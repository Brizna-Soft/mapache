#include "modelo.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <wx/dc.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/settings.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// Elementos e items

wxDataViewItem ItemDe(Carpeta* c)
{
    return wxDataViewItem(c);
}

wxDataViewItem ItemDe(Archivo* a)
{
    return wxDataViewItem(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(a) | 1));
}

wxDataViewItem ItemDe(const Elemento& e)
{
    if (e.carpeta) return ItemDe(e.carpeta);
    if (e.archivo) return ItemDe(e.archivo);
    return wxDataViewItem();
}

Elemento ElementoDe(const wxDataViewItem& item)
{
    Elemento e;
    auto valor = reinterpret_cast<uintptr_t>(item.GetID());
    if (!valor) return e;
    if (valor & 1) e.archivo = reinterpret_cast<Archivo*>(valor & ~uintptr_t(1));
    else e.carpeta = static_cast<Carpeta*>(item.GetID());
    return e;
}

// ---------------------------------------------------------------------------
// Íconos

Iconos& Iconos::Todos()
{
    static Iconos iconos;
    return iconos;
}

wxBitmapBundle Iconos::DelSistema(const wchar_t* ruta, unsigned long atributos, bool soloAtributos)
{
    SHFILEINFOW info{};
    UINT banderas = SHGFI_ICON | SHGFI_SMALLICON | (soloAtributos ? SHGFI_USEFILEATTRIBUTES : 0);
    if (!SHGetFileInfoW(ruta, atributos, &info, sizeof info, banderas) || !info.hIcon) return wxBitmapBundle();
    wxIcon icono;
    if (!icono.CreateFromHICON(info.hIcon))
    {
        DestroyIcon(info.hIcon);
        return wxBitmapBundle();
    }
    return wxBitmapBundle(icono);
}

wxBitmapBundle Iconos::DeCarpeta()
{
    auto it = guardados.find(L"<carpeta>");
    if (it != guardados.end()) return it->second;
    wxBitmapBundle b = DelSistema(L"carpeta", FILE_ATTRIBUTE_DIRECTORY, true);
    guardados[L"<carpeta>"] = b;
    return b;
}

wxBitmapBundle Iconos::DeArchivo(const wchar_t* nombre)
{
    util::ExtensionDe(nombre, extension);
    std::wstring clave = extension.empty() ? std::wstring(L"<archivo>") : extension;
    auto it = guardados.find(clave);
    if (it != guardados.end()) return it->second;
    std::wstring falso = L"archivo" + extension;
    wxBitmapBundle b = DelSistema(falso.c_str(), FILE_ATTRIBUTE_NORMAL, true);
    guardados[clave] = b;
    return b;
}

wxBitmapBundle Iconos::DeUnidad(const std::wstring& raiz)
{
    std::wstring clave = L"<unidad>" + raiz;
    auto it = guardados.find(clave);
    if (it != guardados.end()) return it->second;
    wxBitmapBundle b = DelSistema(raiz.c_str(), 0, false);
    if (!b.IsOk()) b = DelSistema(L"C:\\", FILE_ATTRIBUTE_DIRECTORY, true);
    guardados[clave] = b;
    return b;
}

wxBitmapBundle Iconos::Cuadro(const wxColour& color, int lado)
{
    std::wstring clave = L"<cuadro>" + std::to_wstring(color.GetRGB()) + L"/" + std::to_wstring(lado);
    auto it = guardados.find(clave);
    if (it != guardados.end()) return it->second;
    wxImage imagen(lado, lado);
    imagen.InitAlpha();
    for (int y = 0; y < lado; y++)
        for (int x = 0; x < lado; x++)
        {
            bool borde = x == 0 || y == 0 || x == lado - 1 || y == lado - 1;
            bool esquina = (x == 1 || x == lado - 2) && (y == 1 || y == lado - 2);
            imagen.SetRGB(x, y, color.Red(), color.Green(), color.Blue());
            imagen.SetAlpha(x, y, (borde || esquina) ? 0 : 255);
        }
    wxBitmapBundle b(imagen);
    guardados[clave] = b;
    return b;
}

// ---------------------------------------------------------------------------
// Barra del porcentaje

RenderBarra::RenderBarra()
    : wxDataViewCustomRenderer(L"double", wxDATAVIEW_CELL_INERT, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL)
{
}

bool RenderBarra::SetValue(const wxVariant& valor)
{
    fraccion = valor.IsNull() ? -1.0 : valor.GetDouble();
    return true;
}

bool RenderBarra::GetValue(wxVariant& valor) const
{
    valor = fraccion;
    return true;
}

wxSize RenderBarra::GetSize() const
{
    // Todo el ancho de la columna: si fuera menos, la lista achica la celda y la barra queda corta
    wxSize texto = GetTextExtent(L"100,0 %");
    const wxDataViewColumn* columna = GetOwner();
    int ancho = columna ? columna->GetWidth() : 0;
    return wxSize(std::max(ancho, texto.x + 40), texto.y);
}

bool RenderBarra::Render(wxRect celda, wxDC* dc, int estado)
{
    if (fraccion < 0) return true;
    const wxWindow* vista = GetView();
    int margen = vista ? vista->FromDIP(4) : 4;
    int maxAlto = vista ? vista->FromDIP(11) : 11;
    int anchoTexto = dc->GetTextExtent(L"100,0 %").x;
    int alto = std::clamp(celda.height * 55 / 100, 4, maxAlto);
    wxRect barra(celda.x + margen, celda.y + (celda.height - alto) / 2, celda.width - anchoTexto - 3 * margen, alto);
    wxString texto = util::Porcentaje(fraccion);
    if (barra.width >= 10)
    {
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->SetBrush(wxBrush(util::ColorPista()));
        dc->DrawRectangle(barra);
        int lleno = static_cast<int>(std::lround(barra.width * std::clamp(fraccion, 0.0, 1.0)));
        if (lleno > 0)
        {
            dc->SetBrush(wxBrush(util::Verde()));
            dc->DrawRectangle(barra.x, barra.y, lleno, barra.height);
        }
        RenderText(texto, barra.GetRight() + 1 - celda.x + margen, celda, dc, estado);
    }
    else
    {
        RenderText(texto, 0, celda, dc, estado);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Modelo del árbol

void ModeloArbol::Usar(std::shared_ptr<Resultado> resultado)
{
    datos = std::move(resultado);
    Cleared();
}

void ModeloArbol::GetValue(wxVariant& valor, const wxDataViewItem& item, unsigned int col) const
{
    Elemento e = ElementoDe(item);
    switch (col)
    {
    case columnas::Nombre:
    {
        wxString nombre(e.Nombre());
        wxBitmapBundle icono;
        if (e.carpeta)
        {
            if (e.carpeta->sinAcceso) nombre += L"  (sin permiso)";
            icono = Iconos::Todos().DeCarpeta();
        }
        else if (e.archivo && e.archivo->nombre)
        {
            icono = Iconos::Todos().DeArchivo(e.archivo->nombre);
        }
        valor << wxDataViewIconText(nombre, icono);
        break;
    }
    case columnas::Tamano:
        valor = util::Tamano(e.Tamano());
        break;
    case columnas::Porcentaje:
    {
        Carpeta* padre = e.Padre();
        double f = padre ? (padre->tamano ? double(e.Tamano()) / double(padre->tamano) : 0.0) : 1.0;
        valor = f;
        break;
    }
    case columnas::Archivos:
        valor = e.carpeta ? util::Numero(e.carpeta->archivos) : wxString();
        break;
    case columnas::Modificado:
        valor = util::Fecha(e.Modificado());
        break;
    default:
        valor = wxString();
        break;
    }
}

bool ModeloArbol::GetAttr(const wxDataViewItem& item, unsigned int col, wxDataViewItemAttr& attr) const
{
    Elemento e = ElementoDe(item);
    if (e.carpeta && e.carpeta->sinAcceso)
    {
        attr.SetItalic(true);
        attr.SetColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        return true;
    }
    if (e.carpeta && !e.carpeta->padre && col == columnas::Nombre)
    {
        attr.SetBold(true);
        return true;
    }
    return false;
}

wxDataViewItem ModeloArbol::GetParent(const wxDataViewItem& item) const
{
    Elemento e = ElementoDe(item);
    Carpeta* padre = e.Padre();
    return padre ? ItemDe(padre) : wxDataViewItem();
}

bool ModeloArbol::IsContainer(const wxDataViewItem& item) const
{
    if (!item.IsOk()) return true;
    Elemento e = ElementoDe(item);
    if (!e.carpeta) return false;
    for (const Carpeta* s : e.carpeta->subcarpetas)
        if (!s->quitada) return true;
    for (const Archivo& a : e.carpeta->archivosPropios)
        if (a.nombre) return true;
    return false;
}

unsigned int ModeloArbol::GetChildren(const wxDataViewItem& item, wxDataViewItemArray& hijos) const
{
    if (!datos || !datos->raiz) return 0;
    if (!item.IsOk())
    {
        if (datos->raiz->quitada) return 0;
        hijos.Add(ItemDe(datos->raiz));
        return 1;
    }
    Elemento e = ElementoDe(item);
    if (!e.carpeta) return 0;
    auto& subs = e.carpeta->subcarpetas;
    auto& archs = e.carpeta->archivosPropios;
    size_t i = 0, j = 0;
    while (i < subs.size() || j < archs.size())
    {
        if (i < subs.size() && subs[i]->quitada) { ++i; continue; }
        if (j < archs.size() && !archs[j].nombre) { ++j; continue; }
        if (j >= archs.size() || (i < subs.size() && subs[i]->tamano >= archs[j].tamano)) hijos.Add(ItemDe(subs[i++]));
        else hijos.Add(ItemDe(&archs[j++]));
    }
    return static_cast<unsigned int>(hijos.size());
}

int ModeloArbol::Compare(const wxDataViewItem& item1, const wxDataViewItem& item2, unsigned int col, bool ascendente) const
{
    Elemento a = ElementoDe(item1), b = ElementoDe(item2);
    auto comparar = [](auto x, auto y) { return x < y ? -1 : (x > y ? 1 : 0); };
    int r = 0;
    switch (col)
    {
    case columnas::Nombre:
        r = StrCmpLogicalW(a.Nombre(), b.Nombre());
        break;
    case columnas::Archivos:
        r = comparar(a.carpeta ? a.carpeta->archivos : 0, b.carpeta ? b.carpeta->archivos : 0);
        break;
    case columnas::Modificado:
        r = comparar(a.Modificado(), b.Modificado());
        break;
    default:
        r = comparar(a.Tamano(), b.Tamano());
        break;
    }
    if (r == 0 && col != columnas::Nombre) r = -StrCmpLogicalW(a.Nombre(), b.Nombre());
    if (r == 0) r = comparar(reinterpret_cast<uintptr_t>(item1.GetID()), reinterpret_cast<uintptr_t>(item2.GetID()));
    return ascendente ? r : -r;
}
