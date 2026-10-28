#include "mapa.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr size_t LimiteCajas = 40000;

    struct Rect
    {
        double x, y, w, h;
    };

    // Algoritmo «squarified» (Bruls, Huizing y van Wijk): filas de rectángulos lo más cuadrados posible.
    // Los valores van de mayor a menor y ya están en píxeles cuadrados.
    void Cuadricular(const std::vector<double>& v, Rect r, std::vector<Rect>& salida)
    {
        salida.clear();
        size_t i = 0, n = v.size();
        while (i < n)
        {
            if (r.w <= 0.5 || r.h <= 0.5)
            {
                for (; i < n; ++i) salida.push_back({ r.x, r.y, 0, 0 });
                break;
            }
            double lado = std::min(r.w, r.h), lado2 = lado * lado;
            double suma = 0, peor = std::numeric_limits<double>::max();
            size_t j = i;
            while (j < n)
            {
                double nueva = suma + v[j];
                double nueva2 = nueva * nueva;
                double esta = std::max(lado2 * v[i] / nueva2, nueva2 / (lado2 * v[j]));
                if (j > i && esta > peor) break;
                peor = esta;
                suma = nueva;
                ++j;
            }
            if (r.w >= r.h)
            {
                double ancho = suma / r.h, yy = r.y;
                for (size_t k = i; k < j; ++k)
                {
                    double alto = v[k] / ancho;
                    salida.push_back({ r.x, yy, ancho, alto });
                    yy += alto;
                }
                r.x += ancho;
                r.w -= ancho;
            }
            else
            {
                double alto = suma / r.w, xx = r.x;
                for (size_t k = i; k < j; ++k)
                {
                    double ancho = v[k] / alto;
                    salida.push_back({ xx, r.y, ancho, alto });
                    xx += ancho;
                }
                r.y += alto;
                r.h -= alto;
            }
            i = j;
        }
    }
}

MapaPanel::MapaPanel(wxWindow* padre)
    : wxWindow(padre, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(FromDIP(wxSize(200, 150)));

    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        if (sucio)
        {
            Distribuir();
            Dibujar();
            sucio = false;
        }
        if (lienzo.IsOk()) dc.DrawBitmap(lienzo, 0, 0);
        else
        {
            dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
            dc.Clear();
        }
        if (elegida >= 0 && elegida < static_cast<int>(cajas.size()))
        {
            wxRect r = Redondear(cajas[elegida]);
            bool oscuro = wxSystemSettings::GetAppearance().IsDark();
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(oscuro ? *wxWHITE : *wxBLACK, FromDIP(2)));
            dc.DrawRectangle(r);
        }
    });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        sucio = true;
        Refresh();
        e.Skip();
    });
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& e) {
        sucio = true;
        Refresh();
        e.Skip();
    });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& e) {
        int i = CajaEn(e.GetPosition());
        if (i != senalada)
        {
            senalada = i;
            if (alSenalar) alSenalar(i >= 0 ? cajas[i].e : (elegida >= 0 ? cajas[elegida].e : Elemento()));
        }
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        senalada = -1;
        if (alSenalar) alSenalar(elegida >= 0 && elegida < static_cast<int>(cajas.size()) ? cajas[elegida].e : Elemento());
        e.Skip();
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        SetFocus();
        elegida = CajaEn(e.GetPosition());
        Refresh();
        if (alSenalar && elegida >= 0) alSenalar(cajas[elegida].e);
    });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent& e) {
        int i = CajaEn(e.GetPosition());
        if (i < 0 || !alEntrar) return;
        Carpeta* c = cajas[i].e.carpeta ? cajas[i].e.carpeta : cajas[i].e.Padre();
        if (c && c != actual) alEntrar(c);
    });
    Bind(wxEVT_CONTEXT_MENU, [this](wxContextMenuEvent& e) {
        wxPoint p = e.GetPosition() == wxDefaultPosition ? wxPoint(-1, -1) : ScreenToClient(e.GetPosition());
        int i = p.x >= 0 ? CajaEn(p) : elegida;
        if (i < 0) return;
        elegida = i;
        Refresh();
        Update();
        if (alMenu) alMenu(cajas[i].e);
    });
}

void MapaPanel::Mostrar(std::shared_ptr<Resultado> r, Carpeta* carpeta)
{
    resultado = std::move(r);
    actual = carpeta;
    elegida = senalada = -1;
    sucio = true;
    Refresh();
}

void MapaPanel::Rehacer()
{
    while (actual && actual->quitada) actual = actual->padre;
    elegida = senalada = -1;
    sucio = true;
    Refresh();
}

void MapaPanel::Vaciar()
{
    resultado.reset();
    actual = nullptr;
    cajas.clear();
    elegida = senalada = -1;
    sucio = true;
    Refresh();
}

wxRect MapaPanel::Redondear(const Caja& c)
{
    int x0 = static_cast<int>(std::floor(c.x)), y0 = static_cast<int>(std::floor(c.y));
    int x1 = static_cast<int>(std::floor(c.x + c.w)), y1 = static_cast<int>(std::floor(c.y + c.h));
    return wxRect(x0, y0, x1 - x0, y1 - y0);
}

int MapaPanel::CajaEn(const wxPoint& p) const
{
    for (int i = static_cast<int>(cajas.size()) - 1; i >= 0; --i)
    {
        wxRect r = Redondear(cajas[i]);
        if (r.width >= 1 && r.height >= 1 && r.Contains(p)) return i;
    }
    return -1;
}

void MapaPanel::Distribuir()
{
    cajas.clear();
    elegida = senalada = -1;
    wxSize s = GetClientSize();
    if (!actual || actual->quitada || s.x < 4 || s.y < 4) return;
    cajas.reserve(4096);
    DistribuirEn(actual, 0, 0, s.x, s.y, 0);
}

void MapaPanel::DistribuirEn(Carpeta* c, double x, double y, double w, double h, int nivel)
{
    // Hijos de mayor a menor (carpetas y archivos mezclados)
    std::vector<Elemento> hijos;
    auto& subs = c->subcarpetas;
    auto& archs = c->archivosPropios;
    size_t i = 0, j = 0;
    while (i < subs.size() || j < archs.size())
    {
        if (i < subs.size() && (subs[i]->quitada || subs[i]->tamano == 0)) { ++i; continue; }
        if (j < archs.size() && (!archs[j].nombre || archs[j].tamano == 0)) { ++j; continue; }
        Elemento e;
        if (j >= archs.size() || (i < subs.size() && subs[i]->tamano >= archs[j].tamano)) e.carpeta = subs[i++];
        else e.archivo = &archs[j++];
        hijos.push_back(e);
    }
    if (hijos.empty() || c->tamano == 0) return;

    double area = w * h;
    std::vector<double> valores;
    for (const auto& e : hijos)
    {
        double v = double(e.Tamano()) / double(c->tamano) * area;
        if (v < 2.0) break;   // lo que mide menos de 2 píxeles no se ve: queda como fondo de la carpeta
        valores.push_back(v);
    }
    std::vector<Rect> rects;
    Cuadricular(valores, { x, y, w, h }, rects);

    for (size_t k = 0; k < rects.size(); ++k)
    {
        if (cajas.size() >= LimiteCajas) return;
        const Rect& r = rects[k];
        if (r.w < 0.5 || r.h < 0.5) continue;
        Caja caja{ r.x, r.y, r.w, r.h, hijos[k], util::Categoria::Otros, nivel };
        if (hijos[k].archivo) caja.categoria = util::CategoriaDe(hijos[k].archivo->nombre);
        cajas.push_back(caja);
        if (hijos[k].carpeta)
        {
            double m = (r.w > 10 && r.h > 10) ? 2.0 : ((r.w > 4 && r.h > 4) ? 1.0 : 0.0);
            if (r.w - 2 * m >= 3 && r.h - 2 * m >= 3) DistribuirEn(hijos[k].carpeta, r.x + m, r.y + m, r.w - 2 * m, r.h - 2 * m, nivel + 1);
        }
    }
}

void MapaPanel::Dibujar()
{
    wxSize s = GetClientSize();
    if (s.x <= 0 || s.y <= 0)
    {
        lienzo = wxBitmap();
        return;
    }
    lienzo = wxBitmap(s.x, s.y, 24);
    wxMemoryDC dc(lienzo);
    const wxColour fondo = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    const wxColour texto = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    dc.SetBackground(wxBrush(fondo));
    dc.Clear();
    dc.SetFont(GetFont());

    if (cajas.empty())
    {
        wxString aviso = actual ? L"Esta carpeta está vacía." : L"Analiza una unidad o una carpeta para ver aquí su mapa.";
        wxSize t = dc.GetTextExtent(aviso);
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        dc.DrawText(aviso, (s.x - t.x) / 2, (s.y - t.y) / 2);
        return;
    }

    const wxColour colorCarpeta = wxSystemSettings::SelectLightDark(wxColour(0xD2, 0xD2, 0xD2), wxColour(0x4A, 0x4A, 0x4A));
    const wxColour bordeCarpeta = wxSystemSettings::SelectLightDark(wxColour(0x9A, 0x9A, 0x9A), wxColour(0x26, 0x26, 0x26));
    for (const Caja& c : cajas)
    {
        wxRect r = Redondear(c);
        if (r.width < 1 || r.height < 1) continue;
        if (c.e.carpeta)
        {
            dc.SetPen(r.width > 3 && r.height > 3 ? wxPen(bordeCarpeta) : *wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(colorCarpeta));
            dc.DrawRectangle(r);
            continue;
        }
        wxColour color = util::ColorCategoria(c.categoria);
        if (r.width >= 6 && r.height >= 6)
        {
            // Un poco de relieve: más claro arriba a la izquierda
            dc.GradientFillLinear(r, color.ChangeLightness(130), color.ChangeLightness(90), wxSOUTH);
            dc.SetPen(wxPen(color.ChangeLightness(65)));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(r);
        }
        else
        {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(color));
            dc.DrawRectangle(r);
        }
    }

    // Nombre y tamaño de lo más grande, si cabe
    for (const Caja& c : cajas)
    {
        if (c.nivel != 0) continue;
        wxRect r = Redondear(c);
        wxString etiqueta = wxString(c.e.Nombre()) + L"  " + util::Tamano(c.e.Tamano());
        wxSize t = dc.GetTextExtent(etiqueta);
        int pad = FromDIP(4);
        if (r.width < t.x + 3 * pad || r.height < t.y + 3 * pad) continue;
        wxRect caja(r.x + pad, r.y + pad, t.x + 2 * pad, t.y + pad);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(fondo));
        dc.DrawRoundedRectangle(caja, FromDIP(3));
        dc.SetTextForeground(texto);
        dc.DrawText(etiqueta, caja.x + pad, caja.y + pad / 2);
    }
}
