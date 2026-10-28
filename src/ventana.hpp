#pragma once

// Ventana principal: pestañas «Inicio», «Resultado» y «Limpieza».

#include <wx/frame.h>
#include <wx/timer.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "ajustes.hpp"
#include "analisis.hpp"

class wxButton;
class wxDataViewCtrl;
class wxDataViewListCtrl;
class wxGauge;
class wxNotebook;
class wxSimplebook;
class wxStaticText;
class MapaPanel;
class ModeloArbol;
class PanelLimpieza;

struct Opciones
{
    std::wstring analizar;        // carpeta o unidad para analizar al abrir
    std::wstring carpetaPrueba;   // --limpieza-de-prueba=<carpeta> (oculta, para probar la limpieza)
    std::wstring pestana;         // --pestana=limpieza (oculta)
    bool lento = false;           // --lento (oculta): análisis lento, para probar el progreso
};

class VentanaPrincipal : public wxFrame
{
public:
    explicit VentanaPrincipal(const Opciones& opciones);

private:
    struct Unidad
    {
        std::wstring raiz, etiqueta;
        unsigned tipo = 0;
        uint64_t total = 0, libre = 0;
        bool lista = false;
    };

    // Construcción
    void CrearMenus();
    wxWindow* CrearInicio(wxWindow* padre);
    wxWindow* CrearResultado(wxWindow* padre);
    wxWindow* CrearArbol(wxWindow* padre);
    wxWindow* CrearGrandes(wxWindow* padre);
    wxWindow* CrearTipos(wxWindow* padre);
    wxWindow* CrearMapa(wxWindow* padre);

    // Inicio
    void BuscarUnidades();
    void AnalizarUnidadElegida();
    void ElegirCarpeta();

    // Análisis y resultado
    void Analizar(const std::wstring& ruta);
    void AlTemporizador();
    void AnalisisTerminado(std::shared_ptr<Resultado> r);
    void MostrarResultado(std::shared_ptr<Resultado> r);
    void ActualizarResumen();
    void LlenarGrandes();
    void LlenarTipos();
    void LlenarExtensiones();
    void MostrarEnMapa(Carpeta* carpeta);
    wxString Describir(const Elemento& e) const;

    // Acciones
    Elemento ElementoDeGrandes(int fila) const;
    void MenuDe(const Elemento& e, const wxPoint& dondePantalla);
    void Abrir(const Elemento& e);
    void MostrarEnExplorador(const Elemento& e);
    void CopiarRuta(const Elemento& e);
    void EnviarAPapelera(const Elemento& e);
    void Quitado(const Elemento& e);

    void AlCerrar(wxCloseEvent& e);
    void GuardarAjustes();

    Opciones opciones;
    Ajustes ajustes;

    wxNotebook* libro = nullptr;

    // Inicio
    wxDataViewListCtrl* listaUnidades = nullptr;
    std::vector<Unidad> unidades;
    wxButton* botonAnalizar = nullptr;

    // Resultado
    wxSimplebook* paginas = nullptr;
    wxStaticText* progTitulo = nullptr;
    wxStaticText* progActual = nullptr;
    wxStaticText* progNumeros = nullptr;
    wxGauge* progBarra = nullptr;
    wxStaticText* resTitulo = nullptr;
    wxStaticText* resDetalle = nullptr;
    wxStaticText* resNotas = nullptr;
    wxNotebook* vistas = nullptr;
    wxDataViewCtrl* arbol = nullptr;
    ModeloArbol* modelo = nullptr;
    wxDataViewListCtrl* listaGrandes = nullptr;
    wxDataViewListCtrl* listaTipos = nullptr;
    wxDataViewListCtrl* listaExtensiones = nullptr;
    wxStaticText* tituloExtensiones = nullptr;
    MapaPanel* mapa = nullptr;
    wxStaticText* mapaRuta = nullptr;
    wxStaticText* mapaInfo = nullptr;
    wxButton* botonSubir = nullptr;

    // Limpieza
    PanelLimpieza* limpieza = nullptr;

    Analizador analizador;
    std::shared_ptr<Resultado> resultado;
    std::wstring rutaAnalizada;
    uint64_t usadoUnidad = 0;
    unsigned generacion = 0;
    wxTimer temporizador;
    std::chrono::steady_clock::time_point inicio;
};
