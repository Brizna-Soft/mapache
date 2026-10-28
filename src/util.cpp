#include "util.hpp"

#include <windows.h>

#include <wx/msgdlg.h>
#include <wx/settings.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <unordered_map>

namespace
{
    wchar_t separadorDecimal = L',';
    std::wstring separadorMiles = L".";

    // Número con decimales y el separador decimal de la persona
    std::wstring ConDecimales(double v, int decimales)
    {
        wchar_t texto[64];
        swprintf(texto, 64, L"%.*f", decimales, v);
        for (wchar_t* p = texto; *p; ++p)
            if (*p == L'.' || *p == L',') *p = separadorDecimal;
        return texto;
    }

    std::wstring SinBarraFinal(const std::wstring& ruta)
    {
        std::wstring r = ruta;
        while (r.size() > 1 && r.back() == L'\\' && !util::EsRaiz(r)) r.pop_back();
        return r;
    }
}

void util::PrepararFormato()
{
    wchar_t texto[8];
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, texto, 8) > 1) separadorDecimal = texto[0];
    int n = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, texto, 8);
    if (n > 1) separadorMiles.assign(texto, n - 1);
    if (separadorMiles.size() == 1 && separadorMiles[0] == separadorDecimal)
        separadorMiles = separadorDecimal == L',' ? L"." : L",";
}

wxString util::Tamano(uint64_t bytes)
{
    if (bytes < 1000) return wxString::Format(L"%llu %s", (unsigned long long)bytes, bytes == 1 ? L"byte" : L"bytes");
    static const wchar_t* unidades[] = { L"KB", L"MB", L"GB", L"TB", L"PB", L"EB" };
    double v = double(bytes) / 1024.0;
    int u = 0;
    while (v >= 999.5 && u < 5)
    {
        v /= 1024.0;
        ++u;
    }
    int decimales = v < 9.995 ? 2 : (v < 99.95 ? 1 : 0);
    return wxString(ConDecimales(v, decimales) + L" " + unidades[u]);
}

wxString util::Numero(uint64_t n)
{
    std::wstring cifras = std::to_wstring(n);
    size_t primero = cifras.size() % 3;
    if (primero == 0) primero = 3;
    std::wstring r = cifras.substr(0, primero);
    for (size_t i = primero; i < cifras.size(); i += 3)
    {
        r += separadorMiles;
        r.append(cifras, i, 3);
    }
    return wxString(r);
}

wxString util::Porcentaje(double fraccion)
{
    double v = fraccion * 100.0;
    if (v >= 99.95) return L"100 %";
    if (v < 0) v = 0;
    return wxString(ConDecimales(v, 1) + L" %");
}

wxString util::Fecha(int64_t filetime)
{
    if (filetime <= 0) return wxString();
    FILETIME f;
    f.dwLowDateTime = static_cast<DWORD>(filetime);
    f.dwHighDateTime = static_cast<DWORD>(filetime >> 32);
    SYSTEMTIME utc, local;
    if (!FileTimeToSystemTime(&f, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return wxString();
    wchar_t texto[80];
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, texto, 80, nullptr) == 0) return wxString();
    return texto;
}

wxString util::Duracion(double segundos)
{
    long long s = std::llround(segundos);
    if (s < 1) return L"menos de 1 s";
    if (s < 60) return wxString::Format(L"%lld s", s);
    if (s < 3600) return wxString::Format(L"%lld min %lld s", s / 60, s % 60);
    return wxString::Format(L"%lld h %lld min", s / 3600, (s % 3600) / 60);
}

wxString util::Cantidad(uint64_t n, const wchar_t* uno, const wchar_t* varios)
{
    return Numero(n) + L" " + (n == 1 ? uno : varios);
}

wxString util::RutaCorta(const std::wstring& ruta, size_t maximo)
{
    if (ruta.size() <= maximo) return wxString(ruta);
    // Se deja el principio hasta la primera carpeta («C:\Users\») y el final, cortado en una «\»
    size_t barra = ruta.find(L'\\', 3);
    std::wstring cabeza = (barra == std::wstring::npos || barra > maximo / 2) ? ruta.substr(0, 3) : ruta.substr(0, barra + 1);
    size_t espacio = maximo > cabeza.size() + 2 ? maximo - cabeza.size() - 2 : 16;
    std::wstring cola = ruta.substr(ruta.size() - std::min(espacio, ruta.size()));
    size_t corte = cola.find(L'\\');
    if (corte != std::wstring::npos && corte + 1 < cola.size()) cola = cola.substr(corte + 1);
    return wxString(cabeza + L"\u2026\\" + cola);
}
void util::Avisar(wxWindow* padre, const wxString& texto, const wxString& titulo, long icono, const wxString& detalle)
{
    wxMessageDialog d(padre, texto, titulo, wxOK | icono);
    d.SetOKLabel(L"Aceptar");
    if (!detalle.empty()) d.SetExtendedMessage(detalle);
    d.ShowModal();
}

std::wstring util::RutaLarga(const std::wstring& ruta)
{
    if (ruta.rfind(L"\\\\?\\", 0) == 0) return ruta;
    if (ruta.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + ruta.substr(2);
    return L"\\\\?\\" + ruta;
}

std::wstring util::Unir(const std::wstring& carpeta, const std::wstring& nombre)
{
    if (carpeta.empty()) return nombre;
    if (carpeta.back() == L'\\') return carpeta + nombre;
    return carpeta + L'\\' + nombre;
}

std::wstring util::Normalizar(const std::wstring& ruta)
{
    std::wstring r = ruta;
    for (auto& c : r)
        if (c == L'/') c = L'\\';
    if (r.rfind(L"\\\\?\\UNC\\", 0) == 0) r = L"\\\\" + r.substr(8);
    else if (r.rfind(L"\\\\?\\", 0) == 0) r = r.substr(4);
    // «C:» solo es la carpeta actual de esa unidad para Windows; aquí se entiende como la unidad entera
    if (r.size() == 2 && r[1] == L':') r += L'\\';
    DWORD n = GetFullPathNameW(r.c_str(), 0, nullptr, nullptr);
    if (n > 0)
    {
        std::wstring completa(n, L'\0');
        DWORD m = GetFullPathNameW(r.c_str(), n, completa.data(), nullptr);
        if (m > 0 && m < n)
        {
            completa.resize(m);
            r = completa;
        }
    }
    if (r.size() == 2 && r[1] == L':') r += L'\\';
    r = SinBarraFinal(r);
    if (r.size() >= 2 && r[1] == L':') r[0] = static_cast<wchar_t>(std::towupper(r[0]));
    return r;
}

bool util::EsRaiz(const std::wstring& r)
{
    if (r.size() >= 2 && r.size() <= 3 && r[1] == L':') return r.size() == 2 || r[2] == L'\\';
    if (r.rfind(L"\\\\", 0) == 0)
    {
        size_t a = r.find(L'\\', 2);
        if (a == std::wstring::npos) return true;
        size_t b = r.find(L'\\', a + 1);
        return b == std::wstring::npos || b == r.size() - 1;
    }
    return false;
}

bool util::IgualSinMayusculas(const std::wstring& a, const std::wstring& b)
{
    return a.size() == b.size()
        && CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool util::MismaRuta(const std::wstring& a, const std::wstring& b)
{
    if (a.empty() || b.empty()) return false;
    return IgualSinMayusculas(SinBarraFinal(a), SinBarraFinal(b));
}

bool util::EstaDentro(const std::wstring& ruta, const std::wstring& carpeta)
{
    if (ruta.empty() || carpeta.empty()) return false;
    std::wstring prefijo = SinBarraFinal(carpeta);
    if (prefijo.back() != L'\\') prefijo += L'\\';
    std::wstring r = SinBarraFinal(ruta);
    if (r.size() <= prefijo.size()) return false;
    return CompareStringOrdinal(r.c_str(), static_cast<int>(prefijo.size()), prefijo.c_str(), static_cast<int>(prefijo.size()), TRUE)
        == CSTR_EQUAL;
}

std::wstring util::Padre(const std::wstring& ruta)
{
    std::wstring r = SinBarraFinal(ruta);
    if (EsRaiz(r)) return r;
    size_t i = r.find_last_of(L'\\');
    if (i == std::wstring::npos) return std::wstring();
    std::wstring p = r.substr(0, i);
    if (p.size() == 2 && p[1] == L':') p += L'\\';
    return p;
}

std::wstring util::NombreDe(const std::wstring& ruta)
{
    std::wstring r = SinBarraFinal(ruta);
    if (EsRaiz(r)) return r;
    size_t i = r.find_last_of(L'\\');
    return i == std::wstring::npos ? r : r.substr(i + 1);
}

std::wstring util::RaizDe(const std::wstring& ruta)
{
    if (ruta.size() >= 2 && ruta[1] == L':') return ruta.substr(0, 2) + L"\\";
    if (ruta.rfind(L"\\\\", 0) == 0)
    {
        size_t a = ruta.find(L'\\', 2);
        if (a == std::wstring::npos) return ruta + L"\\";
        size_t b = ruta.find(L'\\', a + 1);
        return b == std::wstring::npos ? ruta + L"\\" : ruta.substr(0, b + 1);
    }
    return std::wstring();
}

bool util::ExisteCarpeta(const std::wstring& ruta)
{
    DWORD a = GetFileAttributesW(RutaLarga(ruta).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool util::Existe(const std::wstring& ruta)
{
    if (GetFileAttributesW(RutaLarga(ruta).c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    DWORD e = GetLastError();
    // Si no se puede saber (por ejemplo, sin permiso), se supone que sigue ahí
    return e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND;
}

int64_t util::Ahora()
{
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    return (static_cast<int64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

void util::ExtensionDe(const wchar_t* nombre, std::wstring& salida)
{
    salida.clear();
    if (!nombre) return;
    const wchar_t* punto = wcsrchr(nombre, L'.');
    if (!punto || punto == nombre || punto[1] == 0) return;
    size_t largo = wcslen(punto);
    if (largo > 16) return;
    for (const wchar_t* p = punto; *p; ++p)
    {
        if (*p == L' ') { salida.clear(); return; }
        salida += static_cast<wchar_t>(std::towlower(*p));
    }
}

util::Categoria util::CategoriaDeExtension(const std::wstring& extension)
{
    static const std::unordered_map<std::wstring, Categoria> tabla = [] {
        std::unordered_map<std::wstring, Categoria> t;
        auto poner = [&t](Categoria c, std::initializer_list<const wchar_t*> lista) {
            for (auto e : lista) t[e] = c;
        };
        poner(Categoria::Videos, { L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".flv", L".webm", L".m4v", L".mpg", L".mpeg",
                                   L".ts", L".m2ts", L".mts", L".3gp", L".vob", L".ogv", L".rmvb", L".divx" });
        poner(Categoria::Imagenes, { L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".heic", L".heif", L".tif", L".tiff",
                                     L".raw", L".cr2", L".cr3", L".nef", L".arw", L".dng", L".orf", L".rw2", L".psd", L".svg",
                                     L".ico", L".avif", L".jxl", L".xcf" });
        poner(Categoria::Musica, { L".mp3", L".wav", L".flac", L".aac", L".m4a", L".ogg", L".opus", L".wma", L".aiff", L".aif",
                                   L".mid", L".midi", L".ape", L".alac", L".m4b" });
        poner(Categoria::Documentos, { L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".odt", L".ods", L".odp",
                                       L".rtf", L".txt", L".csv", L".epub", L".mobi", L".md", L".pub", L".one", L".xps", L".djvu" });
        poner(Categoria::Programas, { L".exe", L".msi", L".dll", L".sys", L".msix", L".msixbundle", L".appx", L".appxbundle",
                                      L".bat", L".cmd", L".ps1", L".com", L".scr", L".cpl", L".ocx", L".drv", L".msp", L".jar",
                                      L".apk" });
        poner(Categoria::Comprimidos, { L".zip", L".rar", L".7z", L".tar", L".gz", L".tgz", L".bz2", L".xz", L".zst", L".iso",
                                        L".img", L".cab", L".lz", L".lzma", L".dmg" });
        return t;
    }();
    auto it = tabla.find(extension);
    return it == tabla.end() ? Categoria::Otros : it->second;
}

util::Categoria util::CategoriaDe(const wchar_t* nombre)
{
    std::wstring e;
    ExtensionDe(nombre, e);
    return CategoriaDeExtension(e);
}

const wchar_t* util::NombreCategoria(Categoria c)
{
    switch (c)
    {
    case Categoria::Videos: return L"Videos";
    case Categoria::Imagenes: return L"Imágenes";
    case Categoria::Musica: return L"Música";
    case Categoria::Documentos: return L"Documentos";
    case Categoria::Programas: return L"Programas";
    case Categoria::Comprimidos: return L"Comprimidos";
    default: return L"Otros";
    }
}

wxColour util::ColorCategoria(Categoria c)
{
    switch (c)
    {
    case Categoria::Videos: return wxColour(0xE8, 0x59, 0x0C);
    case Categoria::Imagenes: return wxColour(0x1C, 0x7E, 0xD6);
    case Categoria::Musica: return wxColour(0xAE, 0x3E, 0xC9);
    case Categoria::Documentos: return wxColour(0xF0, 0x9A, 0x00);
    case Categoria::Programas: return wxColour(0x1D, 0x9E, 0x75);
    case Categoria::Comprimidos: return wxColour(0xC2, 0x25, 0x5C);
    default: return wxColour(0x86, 0x8E, 0x96);
    }
}

wxColour util::Verde()
{
    return wxColour(0x1D, 0x9E, 0x75);
}

wxColour util::ColorPista()
{
    return wxSystemSettings::SelectLightDark(wxColour(0xE4, 0xE4, 0xE4), wxColour(0x45, 0x45, 0x45));
}
