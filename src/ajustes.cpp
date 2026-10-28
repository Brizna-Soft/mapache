#include "ajustes.hpp"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <wx/string.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

#include "papelera.hpp"
#include "util.hpp"

namespace
{
    std::wstring RutaAjustes()
    {
        std::wstring c = Ajustes::Carpeta();
        return c.empty() ? c : util::Unir(c, L"ajustes.json");
    }

    std::string Utf8(const std::wstring& texto)
    {
        return std::string(wxString(texto).utf8_str());
    }

    std::wstring DeUtf8(const std::string& texto)
    {
        return wxString::FromUTF8(texto).ToStdWstring();
    }
}

std::wstring Ajustes::Carpeta()
{
    std::wstring local = papelera::CarpetaConocida(FOLDERID_LocalAppData);
    return local.empty() ? local : util::Unir(local, L"Mapache");
}

void Ajustes::Cargar()
{
    std::wstring ruta = RutaAjustes();
    if (ruta.empty()) return;
    std::ifstream f(ruta, std::ios::binary);
    if (!f) return;
    std::stringstream texto;
    texto << f.rdbuf();
    try
    {
        auto j = nlohmann::json::parse(texto.str());
        if (j.contains("ventana") && j["ventana"].is_object())
        {
            const auto& v = j["ventana"];
            x = v.value("x", 0);
            y = v.value("y", 0);
            ancho = v.value("ancho", 0);
            alto = v.value("alto", 0);
            maximizada = v.value("maximizada", false);
        }
        ultimaCarpeta = DeUtf8(j.value("ultimaCarpeta", std::string()));
    }
    catch (...)
    {
        *this = Ajustes();
    }
}

void Ajustes::Guardar() const
{
    std::wstring carpeta = Carpeta();
    if (carpeta.empty()) return;
    CreateDirectoryW(carpeta.c_str(), nullptr);
    nlohmann::json j;
    j["ventana"] = { { "x", x }, { "y", y }, { "ancho", ancho }, { "alto", alto }, { "maximizada", maximizada } };
    j["ultimaCarpeta"] = Utf8(ultimaCarpeta);
    std::wstring ruta = RutaAjustes();
    std::wstring temporal = ruta + L".nuevo";
    {
        std::ofstream f(temporal, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << j.dump(2);
        if (!f) return;
    }
    MoveFileExW(temporal.c_str(), ruta.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
