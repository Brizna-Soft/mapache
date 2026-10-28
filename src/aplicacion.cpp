// Mapache: mira qué ocupa tu disco y limpia lo que sobra. Hecho por Brizna Soft (licencia MIT).

#include <wx/app.h>
#include <wx/msgdlg.h>

#include "limpieza.hpp"
#include "util.hpp"
#include "ventana.hpp"

class Aplicacion : public wxApp
{
public:
    bool OnInit() override
    {
        // Uso: Mapache.exe [carpeta]
        // Opciones ocultas para probar: --limpieza-de-prueba=<carpeta>, --pestana=limpieza, --tema=claro, --lento
        Opciones opciones;
        bool temaClaro = false;
        for (int i = 1; i < argc; ++i)
        {
            wxString a = argv[i];
            wxString resto;
            if (a.StartsWith(L"--limpieza-de-prueba=", &resto)) opciones.carpetaPrueba = resto.ToStdWstring();
            else if (a.StartsWith(L"--pestana=", &resto)) opciones.pestana = resto.Lower().ToStdWstring();
            else if (a == L"--tema=claro") temaClaro = true;
            else if (a == L"--lento") opciones.lento = true;
            else if (!a.StartsWith(L"-"))
            {
                // «"C:\"» llega como «C:"» (Windows toma \" como una comilla): se quita esa comilla
                a.Trim().Trim(false);
                while (a.EndsWith(L"\"")) a.RemoveLast();
                opciones.analizar = a.ToStdWstring();
            }
        }

        // Tema oscuro o claro, según Windows
        if (!temaClaro) MSWEnableDarkMode();
        SetAppName(L"Mapache");
        SetVendorName(L"Brizna Soft");
        util::PrepararFormato();
        if (!opciones.carpetaPrueba.empty())
        {
            opciones.carpetaPrueba = util::Normalizar(opciones.carpetaPrueba);
            // Para no limpiar por error una carpeta de verdad, la de prueba tiene que tener el archivo de marca
            if (!limpieza::CarpetaDePruebaValida(opciones.carpetaPrueba))
            {
                util::Avisar(nullptr, wxString(L"La carpeta de prueba no tiene el archivo ") + limpieza::MarcaDePrueba
                                          + L". Por seguridad, Mapache no se abre en modo de prueba.",
                             L"Mapache", wxICON_WARNING);
                return false;
            }
        }

        auto* ventana = new VentanaPrincipal(opciones);
        ventana->Show();
        return true;
    }
};

wxIMPLEMENT_APP(Aplicacion);
