#include "ventana.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dataview.h>
#include <wx/dirdlg.h>
#include <wx/display.h>
#include <wx/gauge.h>
#include <wx/iconbndl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include <algorithm>
#include <numeric>

#include "ayuda.hpp"
#include "mapa.hpp"
#include "modelo.hpp"
#include "panel_limpieza.hpp"
#include "papelera.hpp"
#include "version.h"

namespace
{
    enum Pagina { PaginaInicio, PaginaResultado, PaginaLimpieza };
    enum Estado { EstadoVacio, EstadoProgreso, EstadoListo };
    enum Vista { VistaCarpetas, VistaGrandes, VistaTipos, VistaMapa };

    enum
    {
        IdAbrir = wxID_HIGHEST + 100,
        IdExplorador,
        IdCopiar,
        IdVerMapa,
        IdPapelera,
        IdElegirCarpeta,
        IdReanalizar,
        IdInicio,
    };

    wxFont Titulo(wxWindow* w, float escala)
    {
        return w->GetFont().Scaled(escala).Bold();
    }

    const wchar_t* NombreTipoUnidad(unsigned tipo)
    {
        switch (tipo)
        {
        case DRIVE_FIXED: return L"Disco local";
        case DRIVE_REMOVABLE: return L"Unidad extraíble";
        case DRIVE_REMOTE: return L"Unidad de red";
        case DRIVE_CDROM: return L"Unidad de CD o DVD";
        case DRIVE_RAMDISK: return L"Disco RAM";
        default: return L"Unidad";
        }
    }

    wxVariant ConIcono(const wxString& texto, const wxBitmapBundle& icono)
    {
        wxVariant v;
        v << wxDataViewIconText(texto, icono);
        return v;
    }
}

VentanaPrincipal::VentanaPrincipal(const Opciones& op)
    : wxFrame(nullptr, wxID_ANY, wxString::FromUTF8(APP_NOMBRE)), opciones(op), temporizador(this), temporizadorCierre(this)
{
    if (!opciones.carpetaPrueba.empty()) SetTitle(GetTitle() + L" (modo de prueba)");
    SetIcons(wxIconBundle(L"AAAA_APP", nullptr));
    ajustes.Cargar();

    CrearMenus();
    CreateStatusBar(1);

    auto* fondo = new wxPanel(this);
    libro = new wxNotebook(fondo, wxID_ANY);
    libro->AddPage(CrearInicio(libro), L"Inicio", true);
    libro->AddPage(CrearResultado(libro), L"Resultado");
    limpieza = new PanelLimpieza(libro, opciones.carpetaPrueba);
    libro->AddPage(limpieza, L"Limpieza");
    auto* caja = new wxBoxSizer(wxVERTICAL);
    caja->Add(libro, 1, wxEXPAND | wxALL, FromDIP(6));
    fondo->SetSizer(caja);

    libro->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        if (e.GetSelection() == PaginaLimpieza) limpieza->CalcularSiHaceFalta();
        e.Skip();
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent& e) {
        if (e.GetId() == temporizadorCierre.GetId())
        {
            if (!Trabajando())
            {
                temporizadorCierre.Stop();
                Close();
            }
            return;
        }
        AlTemporizador();
    });
    Bind(wxEVT_CLOSE_WINDOW, &VentanaPrincipal::AlCerrar, this);

    // Tecla Supr: enviar a la Papelera lo elegido
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_DELETE && !e.HasAnyModifiers() && resultado && paginas->GetSelection() == EstadoListo)
        {
            wxWindow* foco = wxWindow::FindFocus();
            if (foco && (foco == arbol || arbol->IsDescendant(foco)))
            {
                Elemento el = ElementoDe(arbol->GetSelection());
                if (el)
                {
                    EnviarAPapelera(el);
                    return;
                }
            }
            if (foco && (foco == listaGrandes || listaGrandes->IsDescendant(foco)))
            {
                Elemento el = ElementoDeGrandes(listaGrandes->GetSelectedRow());
                if (el)
                {
                    EnviarAPapelera(el);
                    return;
                }
            }
        }
        e.Skip();
    });

    // Tamaño y lugar de la última vez, si sigue cabiendo en alguna pantalla
    SetMinSize(FromDIP(wxSize(640, 440)));
    if (ajustes.ancho >= 400 && ajustes.alto >= 300
        && wxDisplay::GetFromPoint(wxPoint(ajustes.x + 60, ajustes.y + 20)) != wxNOT_FOUND)
    {
        SetSize(ajustes.x, ajustes.y, ajustes.ancho, ajustes.alto);
    }
    else
    {
        SetSize(FromDIP(wxSize(1000, 620)));
        Centre();
    }
    if (ajustes.maximizada) Maximize();

#ifdef MAPACHE_PRUEBAS
    if (opciones.lento) analizador.PausaPorCarpeta(25);
#endif
    BuscarUnidades();
    SetStatusText(opciones.carpetaPrueba.empty() ? L"Mapache solo mira: no borra nada sin preguntarte."
                                                 : L"Modo de prueba: la limpieza solo toca la carpeta de prueba.");

    if (!opciones.analizar.empty()) CallAfter([this] { Analizar(opciones.analizar); });
    else if (opciones.pestana == L"limpieza") CallAfter([this] { libro->SetSelection(PaginaLimpieza); });
}

// ---------------------------------------------------------------------------
// Construcción

void VentanaPrincipal::CrearMenus()
{
    auto* archivo = new wxMenu();
    archivo->Append(IdElegirCarpeta, L"&Analizar una carpeta...\tCtrl+O");
    archivo->Append(IdReanalizar, L"Analizar de &nuevo\tF5");
    archivo->Append(IdInicio, L"&Volver al inicio");
    archivo->AppendSeparator();
    archivo->Append(wxID_EXIT, L"&Salir");

    auto* barra = new wxMenuBar();
    barra->Append(archivo, L"&Archivo");
    barra->Append(ayuda::CrearMenu(this, L"Muestra qué ocupa espacio en tu disco y limpia, de forma segura, lo que sobra."),
                  L"Ay&uda");
    SetMenuBar(barra);

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ElegirCarpeta(); }, IdElegirCarpeta);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (!rutaAnalizada.empty()) Analizar(rutaAnalizada);
        else libro->SetSelection(PaginaInicio);
    }, IdReanalizar);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { libro->SetSelection(PaginaInicio); }, IdInicio);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
}

wxWindow* VentanaPrincipal::CrearInicio(wxWindow* padre)
{
    auto* p = new wxPanel(padre);
    auto* caja = new wxBoxSizer(wxVERTICAL);
    int m = FromDIP(12);

    auto* titulo = new wxStaticText(p, wxID_ANY, L"¿Qué ocupa espacio en tu disco?");
    titulo->SetFont(Titulo(p, 1.5f));
    caja->Add(titulo, 0, wxLEFT | wxRIGHT | wxTOP, m);
    auto* texto = new wxStaticText(p, wxID_ANY,
        L"Elige una unidad y pulsa «Analizar», o elige una carpeta. Mapache solo mira: no borra nada sin preguntarte.");
    caja->Add(texto, 0, wxLEFT | wxRIGHT | wxTOP, m);

    listaUnidades = new wxDataViewListCtrl(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_SINGLE);
    listaUnidades->AppendIconTextColumn(L"Unidad", wxDATAVIEW_CELL_INERT, FromDIP(250), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    listaUnidades->AppendColumn(
        new wxDataViewColumn(L"Espacio usado", new RenderBarra(), 1, FromDIP(230), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE), L"double");
    listaUnidades->AppendTextColumn(L"Usado", wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);
    listaUnidades->AppendTextColumn(L"Libre", wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);
    listaUnidades->AppendTextColumn(L"Tamaño", wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);
    listaUnidades->AppendTextColumn(L"Tipo", wxDATAVIEW_CELL_INERT, FromDIP(140), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    caja->Add(listaUnidades, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, m);

    auto* fila = new wxBoxSizer(wxHORIZONTAL);
    botonAnalizar = new wxButton(p, wxID_ANY, L"Analizar");
    auto* botonCarpeta = new wxButton(p, wxID_ANY, L"Elegir carpeta...");
    auto* botonActualizar = new wxButton(p, wxID_ANY, L"Actualizar la lista");
    fila->Add(botonAnalizar, 0, wxRIGHT, FromDIP(8));
    fila->Add(botonCarpeta, 0);
    fila->AddStretchSpacer();
    fila->Add(botonActualizar, 0);
    caja->Add(fila, 0, wxEXPAND | wxALL, m);
    p->SetSizer(caja);

    listaUnidades->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent&) { AnalizarUnidadElegida(); });
    listaUnidades->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent&) {
        botonAnalizar->Enable(listaUnidades->GetSelectedRow() != wxNOT_FOUND);
    });
    botonAnalizar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AnalizarUnidadElegida(); });
    botonCarpeta->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ElegirCarpeta(); });
    botonActualizar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BuscarUnidades(); });
    return p;
}

wxWindow* VentanaPrincipal::CrearResultado(wxWindow* padre)
{
    paginas = new wxSimplebook(padre);
    int m = FromDIP(12);

    // Antes de analizar
    auto* vacio = new wxPanel(paginas);
    {
        auto* c = new wxBoxSizer(wxVERTICAL);
        auto* t = new wxStaticText(vacio, wxID_ANY,
            L"Todavía no has analizado nada.\nVe a «Inicio», elige una unidad o una carpeta y pulsa «Analizar».",
            wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
        auto* b = new wxButton(vacio, wxID_ANY, L"Ir a Inicio");
        b->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { libro->SetSelection(PaginaInicio); });
        c->AddStretchSpacer();
        c->Add(t, 0, wxALIGN_CENTER | wxALL, m);
        c->Add(b, 0, wxALIGN_CENTER);
        c->AddStretchSpacer();
        vacio->SetSizer(c);
    }
    paginas->AddPage(vacio, wxString());

    // Mientras analiza
    auto* prog = new wxPanel(paginas);
    {
        auto* c = new wxBoxSizer(wxVERTICAL);
        int g = FromDIP(28);
        progTitulo = new wxStaticText(prog, wxID_ANY, L"Analizando...", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        progTitulo->SetFont(Titulo(prog, 1.3f));
        progActual = new wxStaticText(prog, wxID_ANY, L" ", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        progBarra = new wxGauge(prog, wxID_ANY, 1000, wxDefaultPosition, wxSize(-1, FromDIP(18)));
        progNumeros = new wxStaticText(prog, wxID_ANY, L" ", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        auto* cancelar = new wxButton(prog, wxID_ANY, L"Cancelar");
        cancelar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            analizador.Cancelar();
            progTitulo->SetLabelText(L"Cancelando...");
            SetStatusText(L"Cancelando...");
        });
        c->AddStretchSpacer(1);
        c->Add(progTitulo, 0, wxEXPAND | wxLEFT | wxRIGHT, g);
        c->Add(progActual, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, g);
        c->Add(progBarra, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, g);
        c->Add(progNumeros, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, g);
        c->Add(cancelar, 0, wxALIGN_CENTER | wxTOP, g);
        c->AddStretchSpacer(2);
        prog->SetSizer(c);
    }
    paginas->AddPage(prog, wxString());

    // Resultado
    auto* res = new wxPanel(paginas);
    {
        auto* c = new wxBoxSizer(wxVERTICAL);
        auto* cabecera = new wxBoxSizer(wxHORIZONTAL);
        auto* textos = new wxBoxSizer(wxVERTICAL);
        resTitulo = new wxStaticText(res, wxID_ANY, L" ", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        resTitulo->SetFont(Titulo(res, 1.25f));
        resDetalle = new wxStaticText(res, wxID_ANY, L" ", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        textos->Add(resTitulo, 0, wxEXPAND);
        textos->Add(resDetalle, 0, wxEXPAND | wxTOP, FromDIP(3));
        resNotas = new wxStaticText(res, wxID_ANY, L" ", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        textos->Add(resNotas, 0, wxEXPAND | wxTOP, FromDIP(2));
        cabecera->Add(textos, 1, wxALIGN_CENTER_VERTICAL);
        auto* deNuevo = new wxButton(res, wxID_ANY, L"Analizar de nuevo");
        auto* otra = new wxButton(res, wxID_ANY, L"Analizar otra");
        cabecera->Add(deNuevo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
        cabecera->Add(otra, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
        c->Add(cabecera, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
        deNuevo->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (!rutaAnalizada.empty()) Analizar(rutaAnalizada);
        });
        otra->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { libro->SetSelection(PaginaInicio); });

        vistas = new wxNotebook(res, wxID_ANY);
        vistas->AddPage(CrearArbol(vistas), L"Carpetas");
        vistas->AddPage(CrearGrandes(vistas), L"Archivos más grandes");
        vistas->AddPage(CrearTipos(vistas), L"Por tipo");
        vistas->AddPage(CrearMapa(vistas), L"Mapa");
        c->Add(vistas, 1, wxEXPAND | wxALL, FromDIP(6));
        res->SetSizer(c);
    }
    paginas->AddPage(res, wxString());
    paginas->SetSelection(EstadoVacio);
    return paginas;
}

wxWindow* VentanaPrincipal::CrearArbol(wxWindow* padre)
{
    auto* p = new wxPanel(padre);
    arbol = new wxDataViewCtrl(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_SINGLE);
    modelo = new ModeloArbol();
    arbol->AssociateModel(modelo);
    modelo->DecRef();

    auto* nombre = new wxDataViewColumn(L"Nombre", new wxDataViewIconTextRenderer(), columnas::Nombre, FromDIP(320), wxALIGN_LEFT,
                                        wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    arbol->AppendColumn(nombre);
    arbol->SetExpanderColumn(nombre);
    auto* tamano = arbol->AppendTextColumn(L"Tamaño", columnas::Tamano, wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_RIGHT,
                                           wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    arbol->AppendColumn(new wxDataViewColumn(L"% de la carpeta", new RenderBarra(), columnas::Porcentaje, FromDIP(190),
                                             wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE));
    arbol->AppendTextColumn(L"Archivos", columnas::Archivos, wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_RIGHT,
                            wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    arbol->AppendTextColumn(L"Modificado", columnas::Modificado, wxDATAVIEW_CELL_INERT, FromDIP(100), wxALIGN_LEFT,
                            wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
    tamano->SetSortOrder(false);   // de mayor a menor

    arbol->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [this](wxDataViewEvent& e) {
        if (!e.GetItem().IsOk()) return;
        arbol->Select(e.GetItem());
        MenuDe(ElementoDe(e.GetItem()), e.GetPosition() == wxDefaultPosition ? wxDefaultPosition : arbol->ClientToScreen(e.GetPosition()));
    });
    arbol->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& e) {
        Elemento el = ElementoDe(e.GetItem());
        if (el.carpeta)
        {
            if (arbol->IsExpanded(e.GetItem())) arbol->Collapse(e.GetItem());
            else arbol->Expand(e.GetItem());
        }
        else if (el)
        {
            MostrarEnExplorador(el);
        }
    });

    auto* c = new wxBoxSizer(wxVERTICAL);
    c->Add(arbol, 1, wxEXPAND);
    p->SetSizer(c);
    return p;
}

wxWindow* VentanaPrincipal::CrearGrandes(wxWindow* padre)
{
    auto* p = new wxPanel(padre);
    listaGrandes = new wxDataViewListCtrl(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_SINGLE);
    listaGrandes->AppendIconTextColumn(L"Nombre", wxDATAVIEW_CELL_INERT, FromDIP(260), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    listaGrandes->AppendTextColumn(L"Tamaño", wxDATAVIEW_CELL_INERT, FromDIP(90), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);
    listaGrandes->AppendTextColumn(L"Carpeta", wxDATAVIEW_CELL_INERT, FromDIP(420), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    listaGrandes->AppendTextColumn(L"Modificado", wxDATAVIEW_CELL_INERT, FromDIP(100), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);

    listaGrandes->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [this](wxDataViewEvent& e) {
        if (!e.GetItem().IsOk()) return;
        listaGrandes->Select(e.GetItem());
        MenuDe(ElementoDeGrandes(listaGrandes->ItemToRow(e.GetItem())),
               e.GetPosition() == wxDefaultPosition ? wxDefaultPosition : listaGrandes->ClientToScreen(e.GetPosition()));
    });
    listaGrandes->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& e) {
        Elemento el = ElementoDeGrandes(listaGrandes->ItemToRow(e.GetItem()));
        if (el) MostrarEnExplorador(el);
    });

    auto* c = new wxBoxSizer(wxVERTICAL);
    auto* nota = new wxStaticText(p, wxID_ANY, wxString::Format(L"Los %d archivos que más ocupan. Clic derecho para ver qué hacer con cada uno.",
                                                                 static_cast<int>(Resultado::MaxGrandes)));
    c->Add(nota, 0, wxALL, FromDIP(6));
    c->Add(listaGrandes, 1, wxEXPAND);
    p->SetSizer(c);
    return p;
}

wxWindow* VentanaPrincipal::CrearTipos(wxWindow* padre)
{
    auto* p = new wxPanel(padre);
    listaTipos = new wxDataViewListCtrl(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_SINGLE);
    listaTipos->AppendIconTextColumn(L"Tipo", wxDATAVIEW_CELL_INERT, FromDIP(120), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    listaTipos->AppendTextColumn(L"Tamaño", wxDATAVIEW_CELL_INERT, FromDIP(75), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);
    listaTipos->AppendColumn(new wxDataViewColumn(L"% del total", new RenderBarra(), 2, FromDIP(135), wxALIGN_LEFT,
                                                  wxDATAVIEW_COL_RESIZABLE), L"double");
    listaTipos->AppendTextColumn(L"Archivos", wxDATAVIEW_CELL_INERT, FromDIP(75), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);

    listaExtensiones = new wxDataViewListCtrl(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_SINGLE);
    listaExtensiones->AppendTextColumn(L"Extensión", wxDATAVIEW_CELL_INERT, FromDIP(95), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    listaExtensiones->AppendTextColumn(L"Tamaño", wxDATAVIEW_CELL_INERT, FromDIP(75), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);
    listaExtensiones->AppendColumn(new wxDataViewColumn(L"% del tipo", new RenderBarra(), 2, FromDIP(135), wxALIGN_LEFT,
                                                        wxDATAVIEW_COL_RESIZABLE), L"double");
    listaExtensiones->AppendTextColumn(L"Archivos", wxDATAVIEW_CELL_INERT, FromDIP(75), wxALIGN_RIGHT, wxDATAVIEW_COL_RESIZABLE);

    listaTipos->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent&) { LlenarExtensiones(); });

    auto* izquierda = new wxBoxSizer(wxVERTICAL);
    izquierda->Add(new wxStaticText(p, wxID_ANY, L"Tipos de archivo"), 0, wxBOTTOM, FromDIP(4));
    izquierda->Add(listaTipos, 1, wxEXPAND);
    auto* derecha = new wxBoxSizer(wxVERTICAL);
    tituloExtensiones = new wxStaticText(p, wxID_ANY, L"Extensiones");
    derecha->Add(tituloExtensiones, 0, wxBOTTOM, FromDIP(4));
    derecha->Add(listaExtensiones, 1, wxEXPAND);
    auto* c = new wxBoxSizer(wxHORIZONTAL);
    c->Add(izquierda, 1, wxEXPAND | wxALL, FromDIP(6));
    c->Add(derecha, 1, wxEXPAND | wxALL, FromDIP(6));
    p->SetSizer(c);
    return p;
}

wxWindow* VentanaPrincipal::CrearMapa(wxWindow* padre)
{
    auto* p = new wxPanel(padre);
    auto* c = new wxBoxSizer(wxVERTICAL);
    int m = FromDIP(6);

    auto* arriba = new wxBoxSizer(wxHORIZONTAL);
    botonSubir = new wxButton(p, wxID_ANY, L"Subir un nivel");
    mapaRuta = new wxStaticText(p, wxID_ANY, L" ", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
    arriba->Add(botonSubir, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    arriba->Add(mapaRuta, 1, wxALIGN_CENTER_VERTICAL);
    c->Add(arriba, 0, wxEXPAND | wxALL, m);

    mapa = new MapaPanel(p);
    c->Add(mapa, 1, wxEXPAND | wxLEFT | wxRIGHT, m);

    mapaInfo = new wxStaticText(p, wxID_ANY, L"Pasa el ratón por encima para ver qué es cada rectángulo. Doble clic para entrar en una carpeta.",
                                wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
    c->Add(mapaInfo, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, m);

    auto* leyenda = new wxBoxSizer(wxHORIZONTAL);
    for (int i = 0; i < util::NumCategorias; ++i)
    {
        auto cat = static_cast<util::Categoria>(i);
        leyenda->Add(new wxStaticBitmap(p, wxID_ANY, Iconos::Todos().Cuadro(util::ColorCategoria(cat), FromDIP(12))), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
        leyenda->Add(new wxStaticText(p, wxID_ANY, util::NombreCategoria(cat)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));
    }
    c->Add(leyenda, 0, wxALL, m);
    p->SetSizer(c);

    botonSubir->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (mapa->Actual() && mapa->Actual()->padre) MostrarEnMapa(mapa->Actual()->padre);
    });
    mapa->alSenalar = [this](const Elemento& e) {
        mapaInfo->SetLabelText(e ? Describir(e) : wxString(L"Pasa el ratón por encima para ver qué es cada rectángulo."));
    };
    mapa->alEntrar = [this](Carpeta* carpeta) { MostrarEnMapa(carpeta); };
    mapa->alMenu = [this](const Elemento& e) { MenuDe(e, wxGetMousePosition()); };
    return p;
}

// ---------------------------------------------------------------------------
// Inicio

void VentanaPrincipal::BuscarUnidades()
{
    DWORD modoAnterior = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &modoAnterior);
    unidades.clear();
    wchar_t texto[512] = {};
    DWORD n = GetLogicalDriveStringsW(511, texto);
    for (wchar_t* p = texto; p < texto + n && *p; p += wcslen(p) + 1)
    {
        Unidad u;
        u.raiz = p;
        u.tipo = GetDriveTypeW(p);
        if (u.tipo == DRIVE_NO_ROOT_DIR || u.tipo == DRIVE_UNKNOWN) continue;
        // Las unidades de red pueden tardar mucho en contestar: su espacio se ve al analizarlas
        if (u.tipo != DRIVE_REMOTE)
        {
            wchar_t etiqueta[MAX_PATH + 1] = {};
            if (GetVolumeInformationW(p, etiqueta, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0)) u.etiqueta = etiqueta;
            ULARGE_INTEGER libre{}, total{};
            if (GetDiskFreeSpaceExW(p, &libre, &total, nullptr))
            {
                u.libre = libre.QuadPart;
                u.total = total.QuadPart;
                u.lista = true;
            }
        }
        else
        {
            u.lista = true;
        }
        unidades.push_back(u);
    }
    SetThreadErrorMode(modoAnterior, nullptr);

    listaUnidades->DeleteAllItems();
    for (size_t i = 0; i < unidades.size(); ++i)
    {
        const Unidad& u = unidades[i];
        wxString letra = wxString(u.raiz.substr(0, 2));
        wxString nombre = u.etiqueta.empty() ? wxString(NombreTipoUnidad(u.tipo)) : wxString(u.etiqueta);
        bool conEspacio = u.total > 0;
        wxVector<wxVariant> fila;
        fila.push_back(ConIcono(nombre + L" (" + letra + L")", Iconos::Todos().DeUnidad(u.raiz)));
        fila.push_back(wxVariant(conEspacio ? double(u.total - std::min(u.total, u.libre)) / double(u.total) : -1.0));
        fila.push_back(wxVariant(conEspacio ? util::Tamano(u.total - std::min(u.total, u.libre)) : wxString()));
        fila.push_back(wxVariant(conEspacio ? util::Tamano(u.libre) : wxString()));
        fila.push_back(wxVariant(conEspacio ? util::Tamano(u.total) : wxString(u.lista ? L"" : L"Sin disco")));
        fila.push_back(wxVariant(wxString(NombreTipoUnidad(u.tipo))));
        listaUnidades->AppendItem(fila, static_cast<wxUIntPtr>(i));
    }
    if (!unidades.empty()) listaUnidades->SelectRow(0);
    botonAnalizar->Enable(listaUnidades->GetSelectedRow() != wxNOT_FOUND);
}

void VentanaPrincipal::AnalizarUnidadElegida()
{
    int fila = listaUnidades->GetSelectedRow();
    if (fila == wxNOT_FOUND) return;
    size_t i = static_cast<size_t>(listaUnidades->GetItemData(listaUnidades->RowToItem(fila)));
    if (i >= unidades.size()) return;
    if (!unidades[i].lista)
    {
        util::Avisar(this, L"Esa unidad no tiene un disco puesto.", L"Analizar", wxICON_INFORMATION);
        return;
    }
    Analizar(unidades[i].raiz);
}

void VentanaPrincipal::ElegirCarpeta()
{
    if (analizador.Ocupado()) return;
    wxString inicial = ajustes.ultimaCarpeta.empty() ? wxString(papelera::CarpetaConocida(FOLDERID_Profile))
                                                     : wxString(ajustes.ultimaCarpeta);
    wxDirDialog d(this, L"Elige la carpeta que quieres analizar", inicial, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (d.ShowModal() != wxID_OK) return;
    ajustes.ultimaCarpeta = d.GetPath().ToStdWstring();
    Analizar(ajustes.ultimaCarpeta);
}

// ---------------------------------------------------------------------------
// Análisis

void VentanaPrincipal::Analizar(const std::wstring& rutaPedida)
{
    if (analizador.Ocupado() || enviando || cerrando) return;
    std::wstring ruta = util::Normalizar(rutaPedida);
    if (!util::ExisteCarpeta(ruta))
    {
        util::Avisar(this, L"No encuentro esa carpeta o unidad.", L"Analizar", wxICON_WARNING, util::RutaCorta(ruta));
        return;
    }
    rutaAnalizada = ruta;
    usadoUnidad = 0;
    if (util::EsRaiz(ruta))
    {
        ULARGE_INTEGER libre{}, total{};
        if (GetDiskFreeSpaceExW(util::Unir(ruta, L"").c_str(), &libre, &total, nullptr) && total.QuadPart > libre.QuadPart)
            usadoUnidad = total.QuadPart - libre.QuadPart;
    }

    ++generacion;
    unsigned gen = generacion;
    progTitulo->SetLabelText(L"Analizando " + wxString(ruta));
    progActual->SetLabelText(wxString(ruta));
    progNumeros->SetLabelText(L" ");
    progBarra->SetValue(0);
    paginas->SetSelection(EstadoProgreso);
    libro->SetSelection(PaginaResultado);
    paginas->GetPage(EstadoProgreso)->Layout();
    SetStatusText(L"Analizando...");
    inicio = std::chrono::steady_clock::now();
    temporizador.Start(150);

    analizador.Comenzar(ruta, [this, gen](std::shared_ptr<Resultado> r) {
        CallAfter([this, gen, r] {
            if (gen == generacion) AnalisisTerminado(r);
        });
    });
}

void VentanaPrincipal::AlTemporizador()
{
    if (!analizador.Ocupado()) return;
    auto a = analizador.LeerAvance();
    double segundos = std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count();
    progActual->SetLabelText(wxString(a.actual));
    wxString numeros = util::Cantidad(a.archivos, L"archivo", L"archivos") + L"  ·  " + util::Cantidad(a.carpetas, L"carpeta", L"carpetas")
                       + L"  ·  " + util::Tamano(a.bytes);
    if (a.sinPermiso) numeros += L"  ·  " + util::Numero(a.sinPermiso) + L" sin permiso";
    numeros += L"  ·  " + util::Duracion(segundos);
    progNumeros->SetLabelText(numeros);
    if (usadoUnidad > 0) progBarra->SetValue(static_cast<int>(std::min<uint64_t>(990, a.bytes * 1000 / usadoUnidad)));
    else progBarra->Pulse();
}

void VentanaPrincipal::AnalisisTerminado(std::shared_ptr<Resultado> r)
{
    temporizador.Stop();
    analizador.Esperar();
    if (!r)
    {
        SetStatusText(L"Se canceló el análisis.");
        if (resultado)
        {
            rutaAnalizada = resultado->ruta;
            paginas->SetSelection(EstadoListo);
        }
        else
        {
            paginas->SetSelection(EstadoVacio);
            libro->SetSelection(PaginaInicio);
        }
        return;
    }
    MostrarResultado(r);
    wxString estado = L"Listo: " + util::Tamano(r->raiz->tamano) + L" en " + util::Cantidad(r->raiz->archivos, L"archivo", L"archivos") + L".";
    if (r->raiz->sinPermiso == 1)
        estado += L" Una carpeta no se pudo revisar porque Windows no da permiso.";
    else if (r->raiz->sinPermiso > 1)
        estado += wxString::Format(L" %s carpetas no se pudieron revisar porque Windows no da permiso.", util::Numero(r->raiz->sinPermiso));
    SetStatusText(estado);
}

void VentanaPrincipal::MostrarResultado(std::shared_ptr<Resultado> r)
{
    auto anterior = resultado;
    resultado = r;
    rutaAnalizada = r->ruta;
    modelo->Usar(r);
    LlenarGrandes();
    LlenarTipos();
    MostrarEnMapa(r->raiz);
    anterior.reset();

    ActualizarResumen();
    paginas->SetSelection(EstadoListo);
    vistas->SetSelection(VistaCarpetas);
    wxDataViewItem raiz = ItemDe(r->raiz);
    arbol->Expand(raiz);
    arbol->Select(raiz);
    arbol->EnsureVisible(raiz);
}

void VentanaPrincipal::ActualizarResumen()
{
    if (!resultado || !resultado->raiz) return;
    const Carpeta* raiz = resultado->raiz;
    uint64_t tam = raiz->quitada ? 0 : raiz->tamano;
    resTitulo->SetLabelText(wxString::Format(L"%s  —  %s", wxString(resultado->ruta), util::Tamano(tam)));
    uint64_t archivos = raiz->quitada ? 0 : raiz->archivos, carpetas = raiz->quitada ? 0 : raiz->carpetas;
    wxString detalle = util::Cantidad(archivos, L"archivo", L"archivos");
    if (carpetas) detalle += L" en " + util::Cantidad(carpetas, L"carpeta", L"carpetas");
    if (resultado->totalDisco)
        detalle += wxString::Format(L"  ·  Libre en el disco: %s de %s", util::Tamano(resultado->libreDisco), util::Tamano(resultado->totalDisco));
    detalle += L"  ·  Tardó " + util::Duracion(resultado->segundos);
    resDetalle->SetLabelText(detalle);
    resDetalle->SetToolTip(detalle);

    // Segunda línea, solo si hace falta: lo que no se pudo revisar y lo que está en la nube
    wxString notas;
    if (raiz->sinPermiso && !raiz->quitada)
        notas = raiz->sinPermiso == 1 ? wxString(L"1 carpeta sin permiso (Windows no deja revisarla)")
                                      : util::Numero(raiz->sinPermiso) + L" carpetas sin permiso (Windows no deja revisarlas)";
    if (resultado->enLaNube)
    {
        if (!notas.empty()) notas += L"  ·  ";
        notas += util::Cantidad(resultado->enLaNube, L"archivo", L"archivos") + L" solo en la nube (cuentan 0, no ocupan el disco)";
    }
    resNotas->SetLabelText(notas.empty() ? wxString(L" ") : notas);
    resNotas->SetToolTip(notas);
    resNotas->Show(!notas.empty());
    resNotas->GetParent()->Layout();
}

void VentanaPrincipal::LlenarGrandes()
{
    listaGrandes->DeleteAllItems();
    if (!resultado) return;
    for (Archivo* a : resultado->masGrandes)
    {
        wxVector<wxVariant> fila;
        fila.push_back(ConIcono(a->nombre, Iconos::Todos().DeArchivo(a->nombre)));
        fila.push_back(wxVariant(util::Tamano(a->tamano)));
        fila.push_back(wxVariant(wxString(resultado->RutaRelativa(a->padre))));
        fila.push_back(wxVariant(util::Fecha(a->modificado)));
        listaGrandes->AppendItem(fila, reinterpret_cast<wxUIntPtr>(a));
    }
}

void VentanaPrincipal::LlenarTipos()
{
    listaTipos->DeleteAllItems();
    if (!resultado) return LlenarExtensiones();
    uint64_t total = 0;
    for (const auto& t : resultado->tipos) total += t.tamano;
    std::vector<int> orden(util::NumCategorias);
    std::iota(orden.begin(), orden.end(), 0);
    std::stable_sort(orden.begin(), orden.end(), [this](int a, int b) { return resultado->tipos[a].tamano > resultado->tipos[b].tamano; });
    for (int i : orden)
    {
        const Tipo& t = resultado->tipos[i];
        if (t.archivos == 0) continue;
        auto cat = static_cast<util::Categoria>(i);
        wxVector<wxVariant> fila;
        fila.push_back(ConIcono(util::NombreCategoria(cat), Iconos::Todos().Cuadro(util::ColorCategoria(cat), FromDIP(12))));
        fila.push_back(wxVariant(util::Tamano(t.tamano)));
        fila.push_back(wxVariant(total ? double(t.tamano) / double(total) : 0.0));
        fila.push_back(wxVariant(util::Numero(t.archivos)));
        listaTipos->AppendItem(fila, static_cast<wxUIntPtr>(i));
    }
    if (listaTipos->GetItemCount() > 0) listaTipos->SelectRow(0);
    LlenarExtensiones();
}

void VentanaPrincipal::LlenarExtensiones()
{
    listaExtensiones->DeleteAllItems();
    int fila = listaTipos->GetSelectedRow();
    if (!resultado || fila == wxNOT_FOUND)
    {
        tituloExtensiones->SetLabelText(L"Extensiones");
        return;
    }
    auto cat = static_cast<util::Categoria>(listaTipos->GetItemData(listaTipos->RowToItem(fila)));
    uint64_t totalTipo = resultado->tipos[static_cast<size_t>(cat)].tamano;
    tituloExtensiones->SetLabelText(wxString::Format(L"Extensiones en «%s»", util::NombreCategoria(cat)));
    int n = 0;
    for (const auto& x : resultado->extensiones)
    {
        if (x.categoria != cat) continue;
        if (++n > 500) break;
        wxVector<wxVariant> f;
        f.push_back(wxVariant(x.nombre.empty() ? wxString(L"(sin extensión)") : wxString(x.nombre)));
        f.push_back(wxVariant(util::Tamano(x.tamano)));
        f.push_back(wxVariant(totalTipo ? double(x.tamano) / double(totalTipo) : 0.0));
        f.push_back(wxVariant(util::Numero(x.archivos)));
        listaExtensiones->AppendItem(f);
    }
}

void VentanaPrincipal::MostrarEnMapa(Carpeta* carpeta)
{
    if (!resultado || !carpeta) return;
    mapa->Mostrar(resultado, carpeta);
    mapaRuta->SetLabelText(wxString::Format(L"%s  ·  %s", wxString(resultado->Ruta(carpeta)), util::Tamano(carpeta->tamano)));
    botonSubir->Enable(carpeta->padre != nullptr);
}

wxString VentanaPrincipal::Describir(const Elemento& e) const
{
    if (!resultado || !e) return wxString();
    wxString texto = wxString(resultado->Ruta(e)) + L"  —  " + util::Tamano(e.Tamano());
    if (e.carpeta) texto += L" en " + util::Cantidad(e.carpeta->archivos, L"archivo", L"archivos");
    return texto;
}

// ---------------------------------------------------------------------------
// Acciones

Elemento VentanaPrincipal::ElementoDeGrandes(int fila) const
{
    Elemento e;
    if (fila == wxNOT_FOUND || fila < 0 || !resultado) return e;
    e.archivo = reinterpret_cast<Archivo*>(listaGrandes->GetItemData(listaGrandes->RowToItem(fila)));
    return e;
}

void VentanaPrincipal::MenuDe(const Elemento& e, const wxPoint& dondePantalla)
{
    if (!e || !resultado || enviando || cerrando) return;
    wxMenu menu;
    menu.Append(IdAbrir, e.EsCarpeta() ? L"Abrir la carpeta" : L"Abrir con su programa");
    menu.Append(IdExplorador, L"Mostrar en el Explorador");
    menu.Append(IdCopiar, L"Copiar ruta");
    if (e.EsCarpeta()) menu.Append(IdVerMapa, L"Ver en el mapa");
    menu.AppendSeparator();
    menu.Append(IdPapelera, L"Enviar a la Papelera...");
    // El menú sale donde se hizo clic (con el teclado, donde esté el ratón)
    wxPoint donde = dondePantalla == wxDefaultPosition ? wxDefaultPosition : ScreenToClient(dondePantalla);
    switch (GetPopupMenuSelectionFromUser(menu, donde))
    {
    case IdAbrir: Abrir(e); break;
    case IdExplorador: MostrarEnExplorador(e); break;
    case IdCopiar: CopiarRuta(e); break;
    case IdVerMapa:
        MostrarEnMapa(e.carpeta);
        vistas->SetSelection(VistaMapa);
        break;
    case IdPapelera: EnviarAPapelera(e); break;
    default: break;
    }
}

namespace
{
    // ¿Se puede abrir sin preguntar? Solo lo que hay ahora en esa ruta (no lo que había al analizar):
    // - una carpeta común (sin enlace, sin marca de sistema ni de solo lectura, que son las que Windows puede convertir
    //   en carpetas especiales con desktop.ini);
    // - o un archivo de un tipo de documento, foto, música o video conocido, que Windows no marque como peligroso.
    // Todo lo demás (programas, scripts, accesos directos, tipos desconocidos) se pregunta antes.
    bool SeAbreSinPreguntar(const std::wstring& ruta)
    {
        // Una «{» en cualquier parte de la ruta (también en una carpeta de más arriba) puede ser una carpeta especial de
        // Windows («nombre.{CLSID}») que activa un componente al abrirse: pregunta
        if (ruta.find(L'{') != std::wstring::npos) return false;
        DWORD a = GetFileAttributesW(util::RutaLarga(ruta).c_str());
        if (a == INVALID_FILE_ATTRIBUTES) return false;
        // Una carpeta con «{» en el nombre puede ser una carpeta especial de Windows («nombre.{CLSID}») que activa un
        // componente al abrirse: también pregunta
        if (a & FILE_ATTRIBUTE_DIRECTORY)
            return (a & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) == 0
                && wcschr(PathFindFileNameW(ruta.c_str()), L'{') == nullptr;
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) return false;
        const wchar_t* ext = PathFindExtensionW(ruta.c_str());
        if (!ext || !*ext || AssocIsDangerous(ext)) return false;
        static const wchar_t* const comunes[] = {
            L".txt", L".log", L".md", L".csv", L".pdf", L".rtf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx",
            L".odt", L".ods", L".odp", L".epub", L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".heic", L".tif",
            L".tiff", L".mp3", L".wav", L".flac", L".m4a", L".ogg", L".opus", L".aac", L".wma", L".mp4", L".mkv", L".avi",
            L".mov", L".wmv", L".webm", L".m4v" };
        for (const wchar_t* c : comunes)
        {
            if (_wcsicmp(ext, c) == 0) return true;
        }
        return false;
    }
}

void VentanaPrincipal::Abrir(const Elemento& e)
{
    std::wstring ruta = resultado->Ruta(e);
    if (GetFileAttributesW(util::RutaLarga(ruta).c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        util::Avisar(this, L"Ya no está ahí: puede que se haya movido o borrado después de analizar.", L"Abrir", wxICON_WARNING,
                     util::RutaCorta(ruta));
        return;
    }
    // Mapache solo mira: nada que pueda ejecutar un programa se abre sin preguntar (y «No» va marcado)
    if (!SeAbreSinPreguntar(ruta))
    {
        wxMessageDialog pregunta(this, wxString::Format(L"«%s» puede ejecutar un programa o hacer cambios en la PC. "
                                                        L"¿Seguro que quieres abrirlo?", wxString(e.Nombre())),
                                 L"Abrir", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        pregunta.SetExtendedMessage(util::RutaCorta(ruta) + L"\n\nAbrirlo es como hacerle doble clic en el Explorador. "
                                    L"Hazlo solo si sabes qué es y confías en de dónde vino.");
        pregunta.SetYesNoLabels(L"Abrirlo", L"No");
        if (pregunta.ShowModal() != wxID_YES) return;
    }
    auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(GetHWND(), L"open", ruta.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (r <= 32) util::Avisar(this, L"Windows no pudo abrirlo.", L"Abrir", wxICON_WARNING, util::RutaCorta(ruta));
}

void VentanaPrincipal::MostrarEnExplorador(const Elemento& e)
{
    std::wstring ruta = resultado->Ruta(e);
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(ruta.c_str());
    if (pidl)
    {
        HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        if (SUCCEEDED(hr)) return;
    }
    std::wstring carpeta = e.carpeta ? ruta : util::Padre(ruta);
    ShellExecuteW(GetHWND(), L"open", carpeta.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void VentanaPrincipal::CopiarRuta(const Elemento& e)
{
    std::wstring ruta = resultado->Ruta(e);
    if (wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(ruta));
        wxTheClipboard->Close();
        SetStatusText(wxString(L"Ruta copiada: ") + ruta);
    }
}

void VentanaPrincipal::EnviarAPapelera(const Elemento& e)
{
    if (!e || !resultado || enviando || cerrando) return;
    std::wstring ruta = resultado->Ruta(e);

    std::wstring motivo = papelera::Motivo(ruta);
    if (!motivo.empty())
    {
        util::Avisar(this, L"Mapache no envía esto a la Papelera.", L"No se puede enviar a la Papelera", wxICON_WARNING,
                     wxString(motivo) + L"\n\n" + util::RutaCorta(ruta));
        return;
    }

    wxString cuanto = e.EsCarpeta()
        ? wxString::Format(L"Ocupa %s (%s).", util::Tamano(e.Tamano()), util::Cantidad(e.carpeta->archivos, L"archivo", L"archivos"))
        : wxString::Format(L"Ocupa %s.", util::Tamano(e.Tamano()));
    wxMessageDialog pregunta(this, wxString::Format(L"¿Enviar «%s» a la Papelera?", wxString(e.Nombre())), L"Enviar a la Papelera",
                             wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    pregunta.SetExtendedMessage(cuanto + L"\n" + util::RutaCorta(ruta)
                                + L"\n\nPodrás recuperarlo desde la Papelera de reciclaje. El espacio se libera de verdad "
                                  L"cuando vacías la Papelera (pestaña «Limpieza»).");
    pregunta.SetYesNoLabels(L"Enviar a la Papelera", L"Cancelar");
    if (pregunta.ShowModal() != wxID_YES) return;

    // Windows hace el trabajo en otro hilo (con su propio COM) para que la ventana no se congele.
    // Mientras tanto no se puede analizar, limpiar ni enviar otra cosa.
    if (enviando || analizador.Ocupado()) return;
    if (hiloPapelera.joinable()) hiloPapelera.join();
    enviando = true;
    vistas->Disable();
    wxBeginBusyCursor();
    SetStatusText(wxString::Format(L"Enviando a la Papelera: %s...", wxString(e.Nombre())));
    HWND ventana = GetHWND();
    hiloPapelera = std::thread([this, ventana, ruta, e]() {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::wstring detalle;
        papelera::Respuesta respuesta = papelera::Enviar(ventana, ruta, detalle);
        if (SUCCEEDED(com)) CoUninitialize();
        CallAfter([this, e, ruta, respuesta, detalle] { PapeleraTerminada(e, ruta, respuesta, detalle); });
    });
}

void VentanaPrincipal::PapeleraTerminada(const Elemento& e, const std::wstring& ruta, papelera::Respuesta respuesta,
                                         const std::wstring& detalle)
{
    if (hiloPapelera.joinable()) hiloPapelera.join();
    enviando = false;
    wxEndBusyCursor();
    vistas->Enable();
    if (respuesta == papelera::Respuesta::Enviado)
    {
        wxString nombre(e.Nombre());
        uint64_t tam = e.Tamano();
        Quitado(e);
        SetStatusText(wxString::Format(L"Se envió a la Papelera: %s (%s).", nombre, util::Tamano(tam)));
    }
    else
    {
        SetStatusText(L"No se pudo enviar a la Papelera. No se borró nada.");
        if (!cerrando)
            util::Avisar(this, L"No se pudo enviar a la Papelera.", L"Enviar a la Papelera", wxICON_WARNING,
                         wxString(detalle) + L"\n\n" + util::RutaCorta(ruta));
    }
}

void VentanaPrincipal::Quitado(const Elemento& e)
{
    wxDataViewItem item = ItemDe(e);
    Carpeta* padre = e.Padre();
    resultado->Quitar(e);
    if (!padre)
    {
        // Se fue la carpeta analizada entera
        modelo->Cleared();
    }
    else
    {
        modelo->ItemDeleted(ItemDe(padre), item);
        for (Carpeta* c = padre; c; c = c->padre) modelo->ItemChanged(ItemDe(c));
        modelo->Resort();
    }
    arbol->Refresh();
    LlenarGrandes();
    LlenarTipos();
    mapa->Rehacer();
    if (mapa->Actual()) MostrarEnMapa(mapa->Actual());
    ActualizarResumen();
}

// ---------------------------------------------------------------------------
// Cierre

void VentanaPrincipal::GuardarAjustes()
{
    ajustes.maximizada = IsMaximized();
    if (!IsMaximized() && !IsIconized())
    {
        wxRect r = GetRect();
        ajustes.x = r.x;
        ajustes.y = r.y;
        ajustes.ancho = r.width;
        ajustes.alto = r.height;
    }
    ajustes.Guardar();
}

bool VentanaPrincipal::Trabajando() const
{
    return analizador.Ocupado() || limpieza->Ocupado() || enviando;
}

void VentanaPrincipal::AlCerrar(wxCloseEvent& e)
{
    // Si algo sigue trabajando, se pide que pare y la ventana se cierra sola cuando termina, sin congelarse
    if (Trabajando() && e.CanVeto())
    {
        if (!cerrando)
        {
            cerrando = true;
            analizador.Cancelar();
            limpieza->PedirCancelar();
            progTitulo->SetLabelText(L"Cancelando...");
            SetStatusText(enviando ? L"Se cerrará cuando termine de enviar a la Papelera..."
                                   : L"Cancelando para cerrar...");
            temporizadorCierre.Start(200);
        }
        e.Veto();
        return;
    }
    temporizador.Stop();
    temporizadorCierre.Stop();
    analizador.Cancelar();
    analizador.Esperar();
    limpieza->Detener();
    if (hiloPapelera.joinable()) hiloPapelera.join();
    GuardarAjustes();
    e.Skip();
}
