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

    int64_t Tiempo(const FILETIME& f)
    {
        return (static_cast<int64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    }

    enum class Borrado { Hecho, EnUso, Error };

    // Abre el archivo sin compartirlo con nadie: si otro programa lo tiene abierto, falla y se deja.
    Borrado BorrarArchivo(const std::wstring& ruta)
    {
        HANDLE h = CreateFileW(util::RutaLarga(ruta).c_str(), DELETE | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            DWORD e = GetLastError();
            return (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) ? Borrado::EnUso : Borrado::Error;
        }
        DisposicionEx ex{ BanderaBorrar | BanderaIgnorarSoloLectura };
        BOOL ok = SetFileInformationByHandle(h, ClaseDisposicionEx, &ex, sizeof ex);
        if (!ok)
        {
            FILE_DISPOSITION_INFO d{};
            d.DeleteFile = TRUE;
            ok = SetFileInformationByHandle(h, FileDispositionInfo, &d, sizeof d);
        }
        CloseHandle(h);
        return ok ? Borrado::Hecho : Borrado::Error;
    }

    struct Cuenta
    {
        uint64_t bytes = 0, archivos = 0;
    };

    // Recorre una carpeta (sin seguir uniones ni enlaces). Con «borrar», borra lo que toca y después
    // quita las subcarpetas que quedaron vacías; si no, solo cuenta.
    void Recorrer(const std::wstring& carpeta, bool soloViejos, int64_t limite, bool borrar, Cuenta& cuenta,
                  limpieza::Informe& informe, const std::atomic<bool>& cancelar, int nivel)
    {
        if (nivel > 64 || cancelar) return;
        WIN32_FIND_DATAW d;
        HANDLE h = FindFirstFileExW(util::RutaLarga(util::Unir(carpeta, L"*")).c_str(), FindExInfoBasic, &d,
                                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return;
        struct Sub
        {
            std::wstring ruta;
            bool vieja;
        };
        std::vector<Sub> subcarpetas;
        do
        {
            if (cancelar) break;
            const wchar_t* n = d.cFileName;
            if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
            if (d.dwFileAttributes & NoTocar) continue;
            bool viejo = std::max(Tiempo(d.ftLastWriteTime), Tiempo(d.ftCreationTime)) < limite;
            std::wstring ruta = util::Unir(carpeta, n);
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                subcarpetas.push_back({ ruta, !soloViejos || viejo });
                continue;
            }
            if (soloViejos && !viejo) continue;
            uint64_t tam = (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
            if (!borrar)
            {
                cuenta.bytes += tam;
                cuenta.archivos++;
                continue;
            }
            switch (BorrarArchivo(ruta))
            {
            case Borrado::Hecho:
                informe.liberado += tam;
                informe.borrados++;
                break;
            case Borrado::EnUso:
                informe.enUso++;
                break;
            default:
                informe.fallidos++;
                break;
            }
        } while (FindNextFileW(h, &d));
        FindClose(h);

        for (const auto& s : subcarpetas)
        {
            if (cancelar) break;
            Recorrer(s.ruta, soloViejos, limite, borrar, cuenta, informe, cancelar, nivel + 1);
            // RemoveDirectory solo funciona si la carpeta quedó vacía
            if (borrar && s.vieja) RemoveDirectoryW(util::RutaLarga(s.ruta).c_str());
        }
    }
}

bool limpieza::CarpetaDePruebaValida(const std::wstring& carpeta)
{
    if (carpeta.empty()) return false;
    DWORD a = GetFileAttributesW(util::RutaLarga(util::Unir(carpeta, MarcaDePrueba)).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<limpieza::Grupo> limpieza::Preparar(const std::wstring& carpetaPrueba)
{
    std::vector<Grupo> g(4);
    g[0].tipo = Tipo::Papelera;
    g[1].tipo = Tipo::Temporales;
    g[2].tipo = Tipo::Informes;
    g[3].tipo = Tipo::Sombreadores;

    auto sombreadores = [](const std::wstring& base) {
        return std::vector<Lugar>{ { util::Unir(base, L"D3DSCache"), false },
                                   { util::Unir(base, L"NVIDIA\\DXCache"), false },
                                   { util::Unir(base, L"AMD\\DxCache"), false },
                                   { util::Unir(base, L"AMD\\DxcCache"), false } };
    };

    if (!carpetaPrueba.empty())
    {
        g[0].lugares = { { util::Unir(carpetaPrueba, L"Papelera"), false } };
        g[1].lugares = { { util::Unir(carpetaPrueba, L"Temp"), true } };
        g[2].lugares = { { util::Unir(carpetaPrueba, L"WER"), false } };
        g[3].lugares = sombreadores(carpetaPrueba);
        return g;
    }

    g[0].papeleraReal = true;

    // Temporales: solo si la carpeta se llama Temp o Tmp y no es de Windows ni la carpeta personal
    wchar_t texto[MAX_PATH + 2] = {};
    DWORD n = GetTempPathW(MAX_PATH + 1, texto);
    std::wstring temp = (n > 0 && n <= MAX_PATH + 1) ? util::Normalizar(std::wstring(texto, n)) : std::wstring();
    std::wstring nombre = util::NombreDe(temp);
    if (!temp.empty() && papelera::Motivo(temp).empty()
        && (util::IgualSinMayusculas(nombre, L"Temp") || util::IgualSinMayusculas(nombre, L"Tmp")))
    {
        g[1].lugares = { { temp, true } };
    }
    else
    {
        g[1].disponible = false;
        g[1].aviso = L"Tu carpeta de temporales está en un lugar poco común. Por seguridad no se toca.";
    }

    std::wstring local = papelera::CarpetaConocida(FOLDERID_LocalAppData);
    if (local.empty())
    {
        for (int i : { 2, 3 })
        {
            g[i].disponible = false;
            g[i].aviso = L"No encontré tu carpeta AppData\\Local.";
        }
        return g;
    }
    g[2].lugares = { { util::Unir(local, L"Microsoft\\Windows\\WER"), false } };
    g[3].lugares = sombreadores(local);
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
    int64_t limite = util::Ahora() - UnDia;
    Cuenta cuenta;
    Informe nada;
    for (const auto& l : grupo.lugares)
        if (util::ExisteCarpeta(l.ruta)) Recorrer(l.ruta, l.soloViejos, limite, false, cuenta, nada, cancelar, 0);
    grupo.bytes = cuenta.bytes;
    grupo.archivos = cuenta.archivos;
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
    int64_t limite = util::Ahora() - UnDia;
    Cuenta cuenta;
    for (const auto& l : grupo.lugares)
        if (util::ExisteCarpeta(l.ruta)) Recorrer(l.ruta, l.soloViejos, limite, true, cuenta, informe, cancelar, 0);
}
