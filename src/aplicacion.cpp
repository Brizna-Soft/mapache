// Mapache: mira qué ocupa tu disco y limpia lo que sobra. Hecho por Brizna Soft (licencia MIT).

#include <wx/app.h>
#include <wx/msgdlg.h>

#include "ayuda.hpp"
#include "limpieza.hpp"
#include "util.hpp"
#include "ventana.hpp"

class Aplicacion : public wxApp
{
public:
    bool OnInit() override
    {
        // Uso: Mapache.exe [carpeta]
        // Opciones ocultas: --pestana=limpieza, --tema=claro (solo cambian lo que se ve).
        // Las de prueba (--limpieza-de-prueba=<carpeta>, --lento) solo existen en compilaciones de
        // desarrollo (MAPACHE_PRUEBAS); en lo publicado se ignoran.
        Opciones opciones;
        bool temaClaro = false;
        for (int i = 1; i < argc; ++i)
        {
            wxString a = argv[i];
            wxString resto;
#ifdef MAPACHE_PRUEBAS
            if (a.StartsWith(L"--limpieza-de-prueba=", &resto))
            {
                opciones.carpetaPrueba = resto.ToStdWstring();
                continue;
            }
            if (a == L"--lento")
            {
                opciones.lento = true;
                continue;
            }
#endif
            if (a.StartsWith(L"--pestana=", &resto)) opciones.pestana = resto.Lower().ToStdWstring();
            else if (a == L"--tema=claro") temaClaro = true;
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

#ifdef MAPACHE_PRUEBAS
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
        if (opciones.lento) limpieza::PausaDePrueba(4000);
#endif

        auto* ventana = new VentanaPrincipal(opciones);
        ventana->Show();
        return true;
    }

    int OnExit() override
    {
        ayuda::Cerrar();   // «Buscar actualización» ya no manda nada a la ventana
        return wxApp::OnExit();
    }
};

wxIMPLEMENT_APP(Aplicacion);
