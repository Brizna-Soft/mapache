#include "panel_limpieza.hpp"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>

#include "util.hpp"

namespace
{
    const wchar_t* Titulo(limpieza::Tipo t)
    {
        switch (t)
        {
        case limpieza::Tipo::Papelera: return L"Papelera de reciclaje";
        case limpieza::Tipo::Temporales: return L"Archivos temporales";
        case limpieza::Tipo::Informes: return L"Informes de errores de Windows";
        default: return L"Cachés de sombreadores (juegos)";
        }
    }

    const wchar_t* Descripcion(limpieza::Tipo t, bool prueba)
    {
        switch (t)
        {
        case limpieza::Tipo::Papelera:
            return prueba ? L"Prueba: la carpeta «Papelera» de la carpeta de prueba. La Papelera de Windows no se toca."
                          : L"Lo que ya enviaste a la Papelera, en todas las unidades. Al vaciarla ya no se puede recuperar.";
        case limpieza::Tipo::Temporales:
            return L"Archivos de tu carpeta de temporales con más de 24 horas. Los que algún programa esté usando se dejan.";
        case limpieza::Tipo::Informes:
            return L"Informes que Windows guarda cuando un programa se cierra por un error. Solo sirven para enviarlos a Microsoft.";
        default:
            return L"Datos que la tarjeta de video guarda para que los juegos carguen más rápido. Se vuelven a crear solos; "
                   L"la primera vez que abras un juego puede tardar un poco más.";
        }
    }
}

PanelLimpieza::PanelLimpieza(wxWindow* padre, const std::wstring& carpetaPrueba)
    : wxScrolledWindow(padre, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL), prueba(carpetaPrueba)
{
    SetScrollRate(0, FromDIP(10));
    grupos = limpieza::Preparar(prueba);
    const int m = FromDIP(12);
    auto* caja = new wxBoxSizer(wxVERTICAL);

    auto* titulo = new wxStaticText(this, wxID_ANY, L"Limpieza segura");
    titulo->SetFont(GetFont().Scaled(1.5f).Bold());
    caja->Add(titulo, 0, wxLEFT | wxRIGHT | wxTOP, m);
    auto* intro = new wxStaticText(this, wxID_ANY, wxEmptyString);
    Largo(intro, L"Solo se borran archivos que Windows y los programas vuelven a crear cuando hacen falta. No se tocan tus "
                 L"documentos, fotos ni programas, y nada pide permiso de administrador.", 2 * m);
    caja->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, m);
    if (!prueba.empty())
    {
        auto* aviso = new wxStaticText(this, wxID_ANY, wxEmptyString);
        aviso->SetForegroundColour(wxColour(0xC2, 0x25, 0x5C));
        aviso->SetFont(aviso->GetFont().Bold());
        Largo(aviso, wxString(L"MODO DE PRUEBA: solo se limpia dentro de ") + prueba + L". La Papelera de Windows no se toca.", 2 * m);
        caja->Add(aviso, 0, wxLEFT | wxRIGHT | wxTOP, m);
    }

    auto* rejilla = new wxFlexGridSizer(2, FromDIP(1), FromDIP(24));
    rejilla->AddGrowableCol(0, 1);
    for (const auto& g : grupos)
    {
        Fila f;
        f.casilla = new wxCheckBox(this, wxID_ANY, Titulo(g.tipo));
        f.casilla->SetFont(f.casilla->GetFont().Bold());
        // La Papelera no se marca sola: lo que hay ahí, al vaciarla, ya no se recupera
        f.casilla->SetValue(g.tipo != limpieza::Tipo::Papelera);
        f.tamano = new wxStaticText(this, wxID_ANY, L"Calculando...", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
        f.descripcion = new wxStaticText(this, wxID_ANY, wxEmptyString);
        Largo(f.descripcion, Descripcion(g.tipo, !prueba.empty()), 2 * m + FromDIP(24) + FromDIP(200));
        rejilla->Add(f.casilla, 0, wxALIGN_CENTER_VERTICAL | wxTOP, FromDIP(10));
        rejilla->Add(f.tamano, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxTOP, FromDIP(10));
        rejilla->Add(f.descripcion, 0, wxLEFT, FromDIP(24));
        rejilla->AddSpacer(0);
        f.casilla->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ActualizarTotal(); });
        filas.push_back(f);
    }
    caja->Add(rejilla, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, m);

    total = new wxStaticText(this, wxID_ANY, L" ");
    total->SetFont(total->GetFont().Bold());
    caja->Add(total, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    auto* botones = new wxBoxSizer(wxHORIZONTAL);
    botonLimpiar = new wxButton(this, wxID_ANY, L"Limpiar lo marcado");
    botonCalcular = new wxButton(this, wxID_ANY, L"Calcular de nuevo");
    estado = new wxStaticText(this, wxID_ANY, L" ", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    botones->Add(botonLimpiar, 0, wxRIGHT, FromDIP(8));
    botones->Add(botonCalcular, 0, wxRIGHT, FromDIP(14));
    botones->Add(estado, 1, wxALIGN_CENTER_VERTICAL);
    caja->AddSpacer(FromDIP(8));
    caja->Add(botones, 0, wxEXPAND | wxLEFT | wxRIGHT, m);

    caja->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    auto* herramientas = new wxStaticText(this, wxID_ANY, L"Herramientas de Windows");
    herramientas->SetFont(herramientas->GetFont().Bold());
    caja->Add(herramientas, 0, wxLEFT | wxRIGHT | wxTOP, m);
    auto* textoHerramientas = new wxStaticText(this, wxID_ANY, wxEmptyString);
    Largo(textoHerramientas, L"Para limpiar más (por ejemplo, Windows.old o restos de actualizaciones), usa las herramientas que "
                             L"trae Windows. Pueden pedir permiso de administrador.", 2 * m);
    caja->AddSpacer(FromDIP(4));
    caja->Add(textoHerramientas, 0, wxLEFT | wxRIGHT, m);
    auto* botonesWindows = new wxBoxSizer(wxHORIZONTAL);
    auto* liberador = new wxButton(this, wxID_ANY, L"Liberador de espacio");
    auto* sensor = new wxButton(this, wxID_ANY, L"Sensor de almacenamiento");
    botonesWindows->Add(liberador, 0, wxRIGHT, FromDIP(8));
    botonesWindows->Add(sensor, 0);
    caja->Add(botonesWindows, 0, wxALL, m);
    SetSizer(caja);

    botonLimpiar->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Limpiar(); });
    botonCalcular->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        mensajeFinal.clear();
        Calcular();
    });
    liberador->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        ShellExecuteW(nullptr, L"open", L"cleanmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
    });
    sensor->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        ShellExecuteW(nullptr, L"open", L"ms-settings:storagesense", nullptr, nullptr, SW_SHOWNORMAL);
    });
    // Los textos largos se acomodan al ancho de la ventana
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        Envolver();
        e.Skip();
    });

    MostrarGrupos();
    FitInside();
}

void PanelLimpieza::Largo(wxStaticText* control, const wxString& texto, int sangria)
{
    for (auto& t : textosLargos)
        if (t.control == control)
        {
            t.texto = texto;
            anchoEnvuelto = 0;
            Envolver();
            return;
        }
    textosLargos.push_back({ control, texto, sangria });
    control->SetLabelText(texto);
}

void PanelLimpieza::Envolver()
{
    int ancho = GetClientSize().x;
    if (ancho <= 0 || ancho == anchoEnvuelto) return;
    anchoEnvuelto = ancho;
    for (auto& t : textosLargos)
    {
        t.control->SetLabelText(t.texto);
        t.control->Wrap(std::max(FromDIP(200), ancho - t.sangria));
    }
    Layout();
    FitInside();
}
PanelLimpieza::~PanelLimpieza()
{
    Detener();
}

void PanelLimpieza::Esperar()
{
    if (hilo.joinable()) hilo.join();
}

void PanelLimpieza::Detener()
{
    cancelar = true;
    Esperar();
}

void PanelLimpieza::CalcularSiHaceFalta()
{
    if (!calculado && !trabajando) Calcular();
}

void PanelLimpieza::Ocupar(bool si, const wxString& texto)
{
    trabajando = si;
    botonCalcular->Enable(!si);
    estado->SetLabelText(texto);
    MostrarGrupos();
}

void PanelLimpieza::MostrarGrupos()
{
    for (size_t i = 0; i < grupos.size() && i < filas.size(); ++i)
    {
        const auto& g = grupos[i];
        Fila& f = filas[i];
        f.casilla->Enable(g.disponible && !trabajando);
        wxString t;
        if (!g.disponible) t = L"No disponible";
        else if (!g.calculado) t = trabajando ? L"Calculando..." : L"";
        else if (g.archivos == 0) t = L"Nada que limpiar";
        else
            t = wxString::Format(L"%s  (%s %s)", util::Tamano(g.bytes), util::Numero(g.archivos),
                                 g.tipo == limpieza::Tipo::Papelera ? (g.archivos == 1 ? L"elemento" : L"elementos")
                                                                    : (g.archivos == 1 ? L"archivo" : L"archivos"));
        f.tamano->SetLabelText(t);
        if (!g.disponible && !g.aviso.empty()) Largo(f.descripcion, g.aviso, 0);
    }
    ActualizarTotal();
    Layout();
    FitInside();
}

void PanelLimpieza::ActualizarTotal()
{
    uint64_t suma = 0;
    for (size_t i = 0; i < grupos.size() && i < filas.size(); ++i)
        if (filas[i].casilla->IsChecked() && grupos[i].disponible && grupos[i].calculado) suma += grupos[i].bytes;
    total->SetLabelText(calculado ? wxString(L"Marcado para limpiar: ") + util::Tamano(suma) : wxString(L" "));
    botonLimpiar->Enable(!trabajando && calculado && suma > 0);
}

void PanelLimpieza::Calcular()
{
    if (trabajando) return;
    Esperar();
    auto copia = limpieza::Preparar(prueba);
    cancelar = false;
    Ocupar(true, L"Calculando cuánto se puede liberar...");
    hilo = std::thread([this, copia]() mutable {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        for (auto& g : copia) limpieza::Calcular(g, cancelar);
        if (SUCCEEDED(com)) CoUninitialize();
        CallAfter([this, copia] { Calculado(copia); });
    });
}

void PanelLimpieza::Calculado(const std::vector<limpieza::Grupo>& nuevos)
{
    Esperar();
    grupos = nuevos;
    calculado = true;
    Ocupar(false, mensajeFinal);
}

void PanelLimpieza::Limpiar()
{
    if (trabajando || !calculado) return;
    std::vector<limpieza::Grupo> elegidos;
    uint64_t suma = 0;
    wxString lista;
    bool conPapelera = false;
    for (size_t i = 0; i < grupos.size() && i < filas.size(); ++i)
    {
        const auto& g = grupos[i];
        if (!filas[i].casilla->IsChecked() || !g.disponible || !g.calculado || g.archivos == 0) continue;
        elegidos.push_back(g);
        suma += g.bytes;
        lista += wxString::Format(L"• %s: %s\n", Titulo(g.tipo), util::Tamano(g.bytes));
        if (g.tipo == limpieza::Tipo::Papelera) conPapelera = true;
    }
    if (elegidos.empty())
    {
        util::Avisar(this, L"No hay nada marcado que limpiar.", L"Limpiar", wxICON_INFORMATION);
        return;
    }
    wxString detalle = lista
        + L"\nLos archivos temporales, los informes y las cachés se borran del todo (no van a la Papelera): Windows y "
          L"los programas los vuelven a crear si hacen falta. Los archivos que estén en uso se dejan.";
    if (conPapelera) detalle += L"\n\nLo que está en la Papelera ya no se podrá recuperar.";
    wxMessageDialog pregunta(this, wxString::Format(L"¿Limpiar ahora? Se liberan unos %s.", util::Tamano(suma)),
                             L"Limpiar lo marcado", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
    pregunta.SetExtendedMessage(detalle);
    pregunta.SetYesNoLabels(L"Limpiar", L"Cancelar");
    if (pregunta.ShowModal() != wxID_YES) return;

    Esperar();
    cancelar = false;
    mensajeFinal.clear();
    Ocupar(true, L"Limpiando...");
    hilo = std::thread([this, elegidos]() mutable {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        limpieza::Informe informe;
        for (auto& g : elegidos) limpieza::Limpiar(g, informe, cancelar);
        if (SUCCEEDED(com)) CoUninitialize();
        CallAfter([this, informe] { Limpiado(informe); });
    });
}

void PanelLimpieza::Limpiado(const limpieza::Informe& informe)
{
    Esperar();
    wxString texto = wxString::Format(L"Listo: se liberaron %s.", util::Tamano(informe.liberado));
    if (informe.enUso == 1)
        texto += L" Se dejó 1 archivo porque está en uso.";
    else if (informe.enUso > 1)
        texto += wxString::Format(L" Se dejaron %s archivos porque están en uso.", util::Numero(informe.enUso));
    if (informe.fallidos)
        texto += wxString::Format(L" %s no se %s borrar.", util::Numero(informe.fallidos), informe.fallidos == 1 ? L"pudo" : L"pudieron");
    mensajeFinal = texto;
    trabajando = false;
    Calcular();
}
