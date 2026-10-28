#include "analisis.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_map>

namespace
{
    int64_t Tiempo(const FILETIME& f)
    {
        return (static_cast<int64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    }

    // Atributos de lo que está solo en la nube (OneDrive y similares): no ocupa espacio en el disco
    constexpr DWORD EnLaNube = FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE;
}

// ---------------------------------------------------------------------------
// Almacen

const wchar_t* Almacen::Copiar(const wchar_t* texto, size_t largo)
{
    size_t necesita = largo + 1;
    if (necesita > quedan)
    {
        size_t tam = std::max<size_t>(necesita, 64 * 1024);
        bloques.emplace_back(new wchar_t[tam]);
        libre = bloques.back().get();
        quedan = tam;
    }
    wchar_t* r = libre;
    std::memcpy(r, texto, largo * sizeof(wchar_t));
    r[largo] = L'\0';
    libre += necesita;
    quedan -= necesita;
    return r;
}

Carpeta* Almacen::NuevaCarpeta()
{
    return &carpetas.emplace_back();
}

// ---------------------------------------------------------------------------
// Resultado

std::wstring Resultado::Ruta(const Carpeta* c) const
{
    std::vector<const wchar_t*> partes;
    for (const Carpeta* x = c; x; x = x->padre) partes.push_back(x->nombre);
    if (partes.empty()) return std::wstring();
    std::wstring r = partes.back();
    for (auto it = partes.rbegin() + 1; it != partes.rend(); ++it) r = util::Unir(r, *it);
    return r;
}

std::wstring Resultado::RutaRelativa(const Carpeta* c) const
{
    if (!c || !c->padre) return ruta;
    std::vector<const wchar_t*> partes;
    for (const Carpeta* x = c; x && x->padre; x = x->padre) partes.push_back(x->nombre);
    std::wstring r;
    for (auto it = partes.rbegin(); it != partes.rend(); ++it) r = r.empty() ? std::wstring(*it) : util::Unir(r, *it);
    return r;
}

std::wstring Resultado::Ruta(const Archivo* a) const
{
    return util::Unir(Ruta(a->padre), a->nombre ? a->nombre : L"");
}

std::wstring Resultado::Ruta(const Elemento& e) const
{
    if (e.carpeta) return Ruta(e.carpeta);
    if (e.archivo) return Ruta(e.archivo);
    return std::wstring();
}

void Resultado::Quitar(const Elemento& e)
{
    uint64_t tam = e.Tamano(), archivos = 0, carpetas = 0, sinPermiso = 0;
    Carpeta* padre = e.Padre();
    if (e.carpeta)
    {
        archivos = e.carpeta->archivos;
        carpetas = 1 + e.carpeta->carpetas;
        sinPermiso = e.carpeta->sinPermiso;
        e.carpeta->quitada = true;
    }
    else if (e.archivo)
    {
        archivos = 1;
        e.archivo->nombre = nullptr;
    }
    for (Carpeta* p = padre; p; p = p->padre)
    {
        p->tamano -= std::min(p->tamano, tam);
        p->archivos -= std::min(p->archivos, archivos);
        p->carpetas -= std::min(p->carpetas, carpetas);
        p->sinPermiso -= std::min(p->sinPermiso, sinPermiso);
        std::stable_sort(p->subcarpetas.begin(), p->subcarpetas.end(),
                         [](const Carpeta* a, const Carpeta* b) { return a->tamano > b->tamano; });
    }
    CalcularListas();
}

void Resultado::CalcularListas()
{
    masGrandes.clear();
    tipos = {};
    extensiones.clear();
    if (!raiz || raiz->quitada) return;

    std::unordered_map<std::wstring, size_t> indice;
    auto menor = [](const Archivo* a, const Archivo* b) { return a->tamano > b->tamano; };   // montón de los más chicos
    std::vector<Archivo*> monton;
    monton.reserve(MaxGrandes + 1);
    std::vector<Carpeta*> pila{ raiz };
    std::wstring ext;
    while (!pila.empty())
    {
        Carpeta* c = pila.back();
        pila.pop_back();
        for (Carpeta* s : c->subcarpetas)
            if (!s->quitada) pila.push_back(s);
        for (Archivo& a : c->archivosPropios)
        {
            if (!a.nombre) continue;
            util::ExtensionDe(a.nombre, ext);
            auto [it, nueva] = indice.try_emplace(ext, extensiones.size());
            if (nueva)
            {
                Extension x;
                x.nombre = ext;
                x.categoria = util::CategoriaDeExtension(ext);
                extensiones.push_back(std::move(x));
            }
            Extension& x = extensiones[it->second];
            x.tamano += a.tamano;
            x.archivos++;

            if (monton.size() < MaxGrandes)
            {
                monton.push_back(&a);
                std::push_heap(monton.begin(), monton.end(), menor);
            }
            else if (a.tamano > monton.front()->tamano)
            {
                std::pop_heap(monton.begin(), monton.end(), menor);
                monton.back() = &a;
                std::push_heap(monton.begin(), monton.end(), menor);
            }
        }
    }
    for (const auto& x : extensiones)
    {
        auto& t = tipos[static_cast<size_t>(x.categoria)];
        t.tamano += x.tamano;
        t.archivos += x.archivos;
    }
    std::sort(extensiones.begin(), extensiones.end(), [](const Extension& a, const Extension& b) {
        return a.tamano != b.tamano ? a.tamano > b.tamano : a.nombre < b.nombre;
    });
    std::sort(monton.begin(), monton.end(), [](const Archivo* a, const Archivo* b) { return a->tamano > b->tamano; });
    masGrandes = std::move(monton);
}

// ---------------------------------------------------------------------------
// Analizador

Analizador::~Analizador()
{
    Cancelar();
    Esperar();
}

void Analizador::Comenzar(const std::wstring& ruta, AlTerminar alTerminar)
{
    Esperar();
    cancelar = false;
    ocupado = true;
    cuentaArchivos = 0;
    cuentaCarpetas = 0;
    cuentaBytes = 0;
    cuentaSinPermiso = 0;
    cuentaNube = 0;
    {
        std::lock_guard<std::mutex> l(mutexActual);
        actual = ruta;
    }
    hilo = std::thread(&Analizador::Coordinar, this, ruta, std::move(alTerminar));
}

void Analizador::Cancelar()
{
    {
        std::lock_guard<std::mutex> l(mutexCola);
        cancelar = true;
    }
    condicion.notify_all();
}

void Analizador::Esperar()
{
    if (hilo.joinable()) hilo.join();
}

Analizador::Avance Analizador::LeerAvance()
{
    Avance a;
    a.archivos = cuentaArchivos;
    a.carpetas = cuentaCarpetas;
    a.bytes = cuentaBytes;
    a.sinPermiso = cuentaSinPermiso;
    std::lock_guard<std::mutex> l(mutexActual);
    a.actual = actual;
    return a;
}

void Analizador::Coordinar(std::wstring ruta, AlTerminar alTerminar)
{
    auto inicio = std::chrono::steady_clock::now();
    // Sin ventanas de «inserte un disco» si una unidad no tiene medio
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, nullptr);

    auto r = std::make_shared<Resultado>();
    r->ruta = ruta;
    r->esUnidad = util::EsRaiz(ruta);
    ULARGE_INTEGER libre{}, total{};
    if (GetDiskFreeSpaceExW(util::Unir(ruta, L"").c_str(), &libre, &total, nullptr))
    {
        r->libreDisco = libre.QuadPart;
        r->totalDisco = total.QuadPart;
    }

    auto almacenRaiz = std::make_unique<Almacen>();
    Carpeta* raiz = almacenRaiz->NuevaCarpeta();
    raiz->nombre = almacenRaiz->Copiar(ruta.c_str(), ruta.size());
    WIN32_FILE_ATTRIBUTE_DATA datos;
    if (GetFileAttributesExW(util::RutaLarga(ruta).c_str(), GetFileExInfoStandard, &datos))
        raiz->modificado = Tiempo(datos.ftLastWriteTime);
    r->raiz = raiz;
    r->almacenes.push_back(std::move(almacenRaiz));

    unsigned n = std::clamp(std::thread::hardware_concurrency(), 2u, 8u);
    std::vector<std::unique_ptr<Almacen>> almacenes;
    for (unsigned i = 0; i < n; i++) almacenes.push_back(std::make_unique<Almacen>());
    {
        std::lock_guard<std::mutex> l(mutexCola);
        cola.clear();
        trabajando = 0;
        cola.push_back({ raiz, ruta });
    }
    std::vector<std::thread> hilos;
    for (auto& a : almacenes) hilos.emplace_back(&Analizador::Trabajar, this, std::ref(*a));
    for (auto& h : hilos) h.join();
    for (auto& a : almacenes) r->almacenes.push_back(std::move(a));
    {
        std::lock_guard<std::mutex> l(mutexCola);
        cola.clear();
    }

    if (cancelar)
    {
        r.reset();
        ocupado = false;
        alTerminar(nullptr);
        return;
    }

    // Totales: de las carpetas más hondas hacia la raíz
    std::vector<Carpeta*> todas;
    for (auto& a : r->almacenes)
        for (auto& c : a->Carpetas()) todas.push_back(&c);
    std::sort(todas.begin(), todas.end(), [](const Carpeta* a, const Carpeta* b) { return a->profundidad > b->profundidad; });
    for (Carpeta* c : todas)
    {
        std::sort(c->archivosPropios.begin(), c->archivosPropios.end(),
                  [](const Archivo& a, const Archivo& b) { return a.tamano > b.tamano; });
        uint64_t tam = 0;
        int64_t mod = c->modificado;
        for (const auto& a : c->archivosPropios)
        {
            tam += a.tamano;
            mod = std::max(mod, a.modificado);
        }
        c->tamano = tam;
        c->archivos = c->archivosPropios.size();
        c->carpetas = 0;
        c->sinPermiso = c->sinAcceso ? 1 : 0;
        for (const Carpeta* s : c->subcarpetas)
        {
            c->tamano += s->tamano;
            c->archivos += s->archivos;
            c->carpetas += 1 + s->carpetas;
            c->sinPermiso += s->sinPermiso;
            mod = std::max(mod, s->modificado);
        }
        c->modificado = mod;
        std::sort(c->subcarpetas.begin(), c->subcarpetas.end(), [](const Carpeta* a, const Carpeta* b) { return a->tamano > b->tamano; });
    }
    r->CalcularListas();
    r->enLaNube = cuentaNube;
    r->segundos = std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count();
    ocupado = false;
    alTerminar(r);
}

void Analizador::Trabajar(Almacen& almacen)
{
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, nullptr);
    std::vector<Pendiente> nuevas;
    for (;;)
    {
        Pendiente p;
        {
            std::unique_lock<std::mutex> l(mutexCola);
            condicion.wait(l, [this] { return cancelar || !cola.empty() || trabajando == 0; });
            if (cancelar || cola.empty())
            {
                l.unlock();
                condicion.notify_all();
                return;
            }
            p = std::move(cola.back());
            cola.pop_back();
            ++trabajando;
        }

        nuevas.clear();
        Leer(almacen, p, nuevas);

        bool fin;
        size_t cuantas = nuevas.size();
        {
            std::lock_guard<std::mutex> l(mutexCola);
            --trabajando;
            for (auto& x : nuevas) cola.push_back(std::move(x));
            fin = cola.empty() && trabajando == 0;
        }
        if (fin || cuantas > 1) condicion.notify_all();
        else if (cuantas == 1) condicion.notify_one();
    }
}

void Analizador::Leer(Almacen& almacen, const Pendiente& p, std::vector<Pendiente>& nuevas)
{
    Carpeta* c = p.carpeta;
    if (pausa > 0) Sleep(pausa);

    // Carpeta actual para la ventana (como mucho cada 50 ms)
    uint64_t ahora = GetTickCount64();
    if (ahora - ultimoCambio.load(std::memory_order_relaxed) >= 50 && mutexActual.try_lock())
    {
        actual = p.ruta;
        ultimoCambio = ahora;
        mutexActual.unlock();
    }

    std::wstring patron = util::RutaLarga(util::Unir(p.ruta, L"*"));
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileExW(patron.c_str(), FindExInfoBasic, &d, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    cuentaCarpetas.fetch_add(1, std::memory_order_relaxed);
    if (h == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
        {
            c->sinAcceso = true;
            cuentaSinPermiso.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    uint64_t archivosAqui = 0, bytesAqui = 0, nubeAqui = 0;
    do
    {
        if (cancelar.load(std::memory_order_relaxed)) break;
        const wchar_t* nombre = d.cFileName;
        if (nombre[0] == L'.' && (nombre[1] == 0 || (nombre[1] == L'.' && nombre[2] == 0))) continue;
        size_t largo = wcslen(nombre);
        int64_t mod = Tiempo(d.ftLastWriteTime);

        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // Uniones, enlaces y puntos de montaje apuntan a otro lugar: no se siguen (evita bucles y contar doble)
            if ((d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IsReparseTagNameSurrogate(d.dwReserved0)) continue;
            Carpeta* s = almacen.NuevaCarpeta();
            s->padre = c;
            s->nombre = almacen.Copiar(nombre, largo);
            s->modificado = mod;
            s->profundidad = c->profundidad + 1;
            c->subcarpetas.push_back(s);
            // Carpeta que solo está en la nube: abrirla la bajaría de internet
            if (d.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_OPEN)
            {
                nubeAqui++;
                continue;
            }
            nuevas.push_back({ s, util::Unir(p.ruta, nombre) });
        }
        else
        {
            bool nube = (d.dwFileAttributes & EnLaNube) != 0;
            uint64_t tam = nube ? 0 : ((static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow);
            if (nube) nubeAqui++;
            Archivo a;
            a.padre = c;
            a.nombre = almacen.Copiar(nombre, largo);
            a.tamano = tam;
            a.modificado = mod;
            c->archivosPropios.push_back(a);
            archivosAqui++;
            bytesAqui += tam;
        }
    } while (FindNextFileW(h, &d));
    FindClose(h);

    cuentaArchivos.fetch_add(archivosAqui, std::memory_order_relaxed);
    cuentaBytes.fetch_add(bytesAqui, std::memory_order_relaxed);
    if (nubeAqui) cuentaNube.fetch_add(nubeAqui, std::memory_order_relaxed);
}
