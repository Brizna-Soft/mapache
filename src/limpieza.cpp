#include "limpieza.hpp"

#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>

#include "papelera.hpp"
#include "util.hpp"

namespace
{
    constexpr int64_t UnDia = 24LL * 60 * 60 * 10000000;   // en unidades de 100 ns

    // FileDispositionInfoEx (Windows 10 1809 o más nuevo): borra aunque tenga «solo lectura»
    constexpr FILE_INFO_BY_HANDLE_CLASS ClaseDisposicionEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(21);
    constexpr DWORD BanderaBorrar = 0x1;
    constexpr DWORD BanderaIgnorarSoloLectura = 0x10;
    struct DisposicionEx
    {
        DWORD banderas;
    };

    // Nunca se tocan enlaces, uniones ni archivos que están solo en la nube
    constexpr DWORD NoTocar = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
                              | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE;

    // Carpetas: se abren sin seguir puntos de reanálisis. Sin FILE_SHARE_DELETE, nadie puede
    // renombrarlas ni cambiarlas por una unión mientras están abiertas.
    constexpr DWORD AbrirSinSeguir = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
    constexpr DWORD AccesoLeerCarpeta = FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    constexpr DWORD CompartirSinBorrar = FILE_SHARE_READ | FILE_SHARE_WRITE;

#ifdef MAPACHE_PRUEBAS
    int pausaDePrueba = 0;
#endif

    struct Manija
    {
        HANDLE h = INVALID_HANDLE_VALUE;
        Manija() = default;
        explicit Manija(HANDLE x) : h(x) {}
        Manija(const Manija&) = delete;
        Manija& operator=(const Manija&) = delete;
        ~Manija()
        {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        bool Valida() const { return h != INVALID_HANDLE_VALUE; }
    };

    // Ruta verdadera de algo ya abierto (sin uniones en el medio), sin el prefijo \\?\.
    // Las de red empiezan con «\\». Vacía si Windows no la da.
    std::wstring RutaFinal(HANDLE h)
    {
        std::wstring r(512, L'\0');
        DWORD n = GetFinalPathNameByHandleW(h, r.data(), static_cast<DWORD>(r.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n >= r.size())
        {
            r.assign(n + 1, L'\0');
            n = GetFinalPathNameByHandleW(h, r.data(), static_cast<DWORD>(r.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        }
        if (n == 0 || n >= r.size()) return std::wstring();
        r.resize(n);
        if (r.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + r.substr(8);
        if (r.rfind(L"\\\\?\\", 0) == 0) return r.substr(4);
        return r;
    }

    // Ruta final de una carpeta conocida (siguiendo sus uniones: es la base de confianza)
    std::wstring RutaFinalDe(const std::wstring& ruta)
    {
        Manija m(CreateFileW(util::RutaLarga(ruta).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        return m.Valida() ? RutaFinal(m.h) : std::wstring();
    }

    // ¿Es un punto de reanálisis (unión, enlace...)? Si no se puede saber, se trata como si lo fuera.
    bool EsReanalisis(HANDLE h, bool& esCarpeta)
    {
        FILE_ATTRIBUTE_TAG_INFO info{};
        if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &info, sizeof info)) return true;
        esCarpeta = (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    bool EnDiscoLocal(const std::wstring& rutaFinal)
    {
        if (rutaFinal.empty() || rutaFinal.rfind(L"\\\\", 0) == 0) return false;
        return GetDriveTypeW(util::RaizDe(rutaFinal).c_str()) == DRIVE_FIXED;
    }

    bool DentroOIgual(const std::wstring& ruta, const std::wstring& base)
    {
        return !ruta.empty() && !base.empty() && (util::MismaRuta(ruta, base) || util::EstaDentro(ruta, base));
    }

    enum class Apertura { Bien, Omitida, Error };

    // Abre una carpeta que tiene que estar dentro de «base» (o ser «base» si se permite)
    HANDLE AbrirCarpeta(const std::wstring& ruta, const std::wstring& base, bool puedeSerLaBase, DWORD acceso, DWORD compartir,
                        Apertura& como, std::wstring* rutaFinal = nullptr)
    {
        HANDLE h = CreateFileW(util::RutaLarga(ruta).c_str(), acceso, compartir, nullptr, OPEN_EXISTING, AbrirSinSeguir, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            como = Apertura::Error;
            return h;
        }
        bool carpeta = false;
        std::wstring real = EsReanalisis(h, carpeta) || !carpeta ? std::wstring() : RutaFinal(h);
        bool bien = !real.empty() && EnDiscoLocal(real)
                    && (puedeSerLaBase ? DentroOIgual(real, base) : util::EstaDentro(real, base));
        if (!bien)
        {
            CloseHandle(h);
            como = Apertura::Omitida;
            return INVALID_HANDLE_VALUE;
        }
        if (rutaFinal) *rutaFinal = real;
        como = Apertura::Bien;
        return h;
    }

    enum class Borrado { Hecho, EnUso, Omitido, Error };

    // Abre el archivo sin compartirlo con nadie (si otro programa lo tiene abierto, falla y se deja) y sin
    // seguir enlaces; comprueba con el archivo abierto que está dentro de la raíz y lo borra con ese mismo
    // identificador, así no puede ser otro.
    Borrado BorrarArchivo(const std::wstring& ruta, const std::wstring& raiz)
    {
        Manija m(CreateFileW(util::RutaLarga(ruta).c_str(), DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, 0, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (!m.Valida())
        {
            DWORD e = GetLastError();
            return (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) ? Borrado::EnUso : Borrado::Error;
        }
        bool carpeta = false;
        if (EsReanalisis(m.h, carpeta) || carpeta || !util::EstaDentro(RutaFinal(m.h), raiz)) return Borrado::Omitido;
        DisposicionEx ex{ BanderaBorrar | BanderaIgnorarSoloLectura };
        BOOL ok = SetFileInformationByHandle(m.h, ClaseDisposicionEx, &ex, sizeof ex);
        if (!ok)
        {
            FILE_DISPOSITION_INFO d{};
            d.DeleteFile = TRUE;
            ok = SetFileInformationByHandle(m.h, FileDispositionInfo, &d, sizeof d);
        }
        return ok ? Borrado::Hecho : Borrado::Error;
    }

    // Quita una subcarpeta si quedó vacía (Windows no deja borrar una carpeta con algo dentro)
    void BorrarCarpetaVacia(const std::wstring& ruta, const std::wstring& raiz)
    {
        Apertura como;
        Manija m(AbrirCarpeta(ruta, raiz, false, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, como));
        if (!m.Valida()) return;
        FILE_DISPOSITION_INFO d{};
        d.DeleteFile = TRUE;
        SetFileInformationByHandle(m.h, FileDispositionInfo, &d, sizeof d);
    }

    // Lee el contenido de una carpeta ya abierta (no por su ruta: así no la pueden cambiar en medio)
    template <typename Funcion>
    void Listar(HANDLE carpeta, Funcion&& cada)
    {
        std::vector<unsigned char> memoria(64 * 1024);
        FILE_INFO_BY_HANDLE_CLASS clase = FileFullDirectoryRestartInfo;
        for (;;)
        {
            if (!GetFileInformationByHandleEx(carpeta, clase, memoria.data(), static_cast<DWORD>(memoria.size()))) return;
            clase = FileFullDirectoryInfo;
            size_t desde = 0;
            for (;;)
            {
                auto* d = reinterpret_cast<const FILE_FULL_DIR_INFO*>(memoria.data() + desde);
                std::wstring nombre(d->FileName, d->FileNameLength / sizeof(wchar_t));
                if (!cada(*d, nombre)) return;
                if (d->NextEntryOffset == 0) break;
                desde += d->NextEntryOffset;
            }
        }
    }

    struct Contexto
    {
        std::wstring raiz;             // ruta final de la raíz del lugar
        bool soloViejos = false;
        int64_t limite = 0;
        bool borrar = false;
        uint64_t bytes = 0, archivos = 0;
        limpieza::Informe* informe = nullptr;
        const std::atomic<bool>* cancelar = nullptr;
    };

    // Recorre una carpeta ya abierta y validada. Con «borrar», borra lo que toca y después quita las
    // subcarpetas que quedaron vacías; si no, solo cuenta.
    void Recorrer(Contexto& c, const std::wstring& ruta, HANDLE carpeta, int nivel)
    {
        if (nivel > 64 || *c.cancelar) return;
        struct Sub
        {
            std::wstring ruta;
            bool vieja;
        };
        std::vector<Sub> subcarpetas;
        Listar(carpeta, [&](const FILE_FULL_DIR_INFO& d, const std::wstring& nombre) {
            if (*c.cancelar) return false;
            if (nombre == L"." || nombre == L"..") return true;
            if (d.FileAttributes & NoTocar) return true;
            bool viejo = std::max(d.LastWriteTime.QuadPart, d.CreationTime.QuadPart) < c.limite;
            std::wstring hijo = util::Unir(ruta, nombre);
            if (d.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                subcarpetas.push_back({ hijo, !c.soloViejos || viejo });
                return true;
            }
            if (c.soloViejos && !viejo) return true;
            uint64_t tam = static_cast<uint64_t>(d.EndOfFile.QuadPart);
            if (!c.borrar)
            {
                c.bytes += tam;
                c.archivos++;
                return true;
            }
            switch (BorrarArchivo(hijo, c.raiz))
            {
            case Borrado::Hecho:
                c.informe->liberado += tam;
                c.informe->borrados++;
                break;
            case Borrado::EnUso:
                c.informe->enUso++;
                break;
            case Borrado::Omitido:
                c.informe->omitidos++;
                break;
            default:
                c.informe->fallidos++;
                break;
            }
            return true;
        });

#ifdef MAPACHE_PRUEBAS
        if (c.borrar && nivel == 0 && pausaDePrueba > 0) Sleep(pausaDePrueba);
#endif

        for (const auto& s : subcarpetas)
        {
            if (*c.cancelar) break;
            Apertura como;
            Manija sub(AbrirCarpeta(s.ruta, c.raiz, false, AccesoLeerCarpeta, CompartirSinBorrar, como));
            if (!sub.Valida())
            {
                if (como == Apertura::Omitida && c.borrar) c.informe->omitidos++;
                continue;
            }
            Recorrer(c, s.ruta, sub.h, nivel + 1);
            if (c.borrar && s.vieja)
            {
                CloseHandle(sub.h);
                sub.h = INVALID_HANDLE_VALUE;
                BorrarCarpetaVacia(s.ruta, c.raiz);
            }
        }
    }

    // Abre la raíz de un lugar: no puede ser una unión, tiene que estar en un disco local y dentro de
    // su carpeta permitida (o ser ella misma).
    HANDLE AbrirRaiz(const limpieza::Lugar& l, std::wstring& rutaFinal, Apertura& como)
    {
        return AbrirCarpeta(l.ruta, l.permitida, true, AccesoLeerCarpeta, CompartirSinBorrar, como, &rutaFinal);
    }

    const wchar_t* AvisoLugarRaro(limpieza::Tipo t)
    {
        return t == limpieza::Tipo::Temporales ? limpieza::AvisoTemporalRaro
                                               : L"Esta carpeta no está donde debería (puede ser un enlace a otro lugar); Mapache no la toca.";
    }
}

#ifdef MAPACHE_PRUEBAS
bool limpieza::CarpetaDePruebaValida(const std::wstring& carpeta)
{
    if (carpeta.empty()) return false;
    DWORD a = GetFileAttributesW(util::RutaLarga(util::Unir(carpeta, MarcaDePrueba)).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void limpieza::PausaDePrueba(int milisegundos)
{
    pausaDePrueba = milisegundos;
}
#endif

std::vector<limpieza::Grupo> limpieza::Preparar(const std::wstring& carpetaPrueba)
{
    std::vector<Grupo> g(4);
    g[0].tipo = Tipo::Papelera;
    g[1].tipo = Tipo::Temporales;
    g[2].tipo = Tipo::Informes;
    g[3].tipo = Tipo::Sombreadores;

    auto sombreadores = [](const std::wstring& base, const std::wstring& permitida) {
        return std::vector<Lugar>{ { util::Unir(base, L"D3DSCache"), false, permitida },
                                   { util::Unir(base, L"NVIDIA\\DXCache"), false, permitida },
                                   { util::Unir(base, L"AMD\\DxCache"), false, permitida },
                                   { util::Unir(base, L"AMD\\DxcCache"), false, permitida } };
    };

#ifdef MAPACHE_PRUEBAS
    if (!carpetaPrueba.empty())
    {
        std::wstring base = RutaFinalDe(carpetaPrueba);
        g[0].lugares = { { util::Unir(carpetaPrueba, L"Papelera"), false, base } };
        g[1].lugares = { { util::Unir(carpetaPrueba, L"Temp"), true, base } };
        g[2].lugares = { { util::Unir(carpetaPrueba, L"WER"), false, base } };
        g[3].lugares = sombreadores(carpetaPrueba, base);
        return g;
    }
#else
    (void)carpetaPrueba;   // en lo publicado no hay modo de prueba
#endif

    g[0].papeleraReal = true;

    std::wstring local = papelera::CarpetaConocida(FOLDERID_LocalAppData);
    std::wstring localFinal = local.empty() ? std::wstring() : RutaFinalDe(local);
    if (local.empty() || localFinal.empty() || !EnDiscoLocal(localFinal))
    {
        for (int i : { 1, 2, 3 })
        {
            g[i].disponible = false;
            g[i].aviso = L"No encontré tu carpeta AppData\\Local en este disco.";
        }
        return g;
    }

    // Temporales: solo si la carpeta temporal está dentro de %LOCALAPPDATA%\Temp (o es ella), en un
    // disco local y no es una unión. GetTempPath sale de las variables TMP y TEMP, que se pueden cambiar.
    wchar_t texto[MAX_PATH + 2] = {};
    DWORD n = GetTempPathW(MAX_PATH + 1, texto);
    std::wstring temp = (n > 0 && n <= MAX_PATH + 1) ? util::Normalizar(std::wstring(texto, n)) : std::wstring();
    std::wstring tempPermitida = RutaFinalDe(util::Unir(local, L"Temp"));
    Lugar lugarTemp{ temp, true, tempPermitida };
    std::wstring tempFinal;
    Apertura como = Apertura::Error;
    if (!temp.empty() && !tempPermitida.empty())
    {
        Manija m(AbrirRaiz(lugarTemp, tempFinal, como));
    }
    if (como == Apertura::Bien)
    {
        g[1].lugares = { lugarTemp };
    }
    else
    {
        g[1].disponible = false;
        g[1].aviso = AvisoTemporalRaro;
    }

    g[2].lugares = { { util::Unir(local, L"Microsoft\\Windows\\WER"), false, localFinal } };
    g[3].lugares = sombreadores(local, localFinal);
    return g;
}

void limpieza::Calcular(Grupo& grupo, const std::atomic<bool>& cancelar)
{
    grupo.bytes = 0;
    grupo.archivos = 0;
    grupo.calculado = false;
    if (!grupo.disponible) return;
    if (grupo.papeleraReal)
    {
        SHQUERYRBINFO info{};
        info.cbSize = sizeof info;
        if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info)))
        {
            grupo.bytes = static_cast<uint64_t>(info.i64Size);
            grupo.archivos = static_cast<uint64_t>(info.i64NumItems);
        }
        grupo.calculado = true;
        return;
    }
    Contexto c;
    c.limite = util::Ahora() - UnDia;
    c.cancelar = &cancelar;
    Informe nada;
    c.informe = &nada;
    size_t existen = 0, raros = 0;
    for (const auto& l : grupo.lugares)
    {
        std::wstring real;
        Apertura como;
        Manija raiz(AbrirRaiz(l, real, como));
        if (como == Apertura::Error) continue;   // no existe en esta PC
        existen++;
        if (!raiz.Valida())
        {
            raros++;
            continue;
        }
        c.raiz = real;
        c.soloViejos = l.soloViejos;
        Recorrer(c, l.ruta, raiz.h, 0);
    }
    // Si todo lo que hay está en un lugar raro, el grupo queda apagado con su explicación
    if (existen > 0 && raros == existen)
    {
        grupo.disponible = false;
        grupo.aviso = AvisoLugarRaro(grupo.tipo);
        return;
    }
    grupo.bytes = c.bytes;
    grupo.archivos = c.archivos;
    grupo.calculado = !cancelar;
}

void limpieza::Limpiar(Grupo& grupo, Informe& informe, const std::atomic<bool>& cancelar)
{
    if (!grupo.disponible || cancelar) return;
    if (grupo.papeleraReal)
    {
        SHQUERYRBINFO info{};
        info.cbSize = sizeof info;
        if (FAILED(SHQueryRecycleBinW(nullptr, &info)) || info.i64NumItems == 0) return;
        if (SUCCEEDED(SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND)))
        {
            informe.liberado += static_cast<uint64_t>(info.i64Size);
            informe.borrados += static_cast<uint64_t>(info.i64NumItems);
        }
        else
        {
            informe.fallidos++;
        }
        return;
    }
    Contexto c;
    c.limite = util::Ahora() - UnDia;
    c.borrar = true;
    c.cancelar = &cancelar;
    c.informe = &informe;
    for (const auto& l : grupo.lugares)
    {
        if (cancelar) break;
        // Se vuelve a comprobar justo antes de borrar
        std::wstring real;
        Apertura como;
        Manija raiz(AbrirRaiz(l, real, como));
        if (!raiz.Valida())
        {
            if (como == Apertura::Omitida) informe.omitidos++;
            continue;
        }
        c.raiz = real;
        c.soloViejos = l.soloViejos;
        Recorrer(c, l.ruta, raiz.h, 0);
    }
}
