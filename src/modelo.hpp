#pragma once

// Lo que une el resultado del análisis con las listas de la ventana: el modelo del árbol,
// la barra dibujada del porcentaje y los íconos.

#include <wx/bmpbndl.h>
#include <wx/dataview.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "analisis.hpp"

namespace columnas
{
    enum { Nombre, Tamano, Porcentaje, Archivos, Modificado };
}

// Un archivo y una carpeta no pueden tener la misma dirección; a los archivos se les marca el último bit
wxDataViewItem ItemDe(Carpeta* c);
wxDataViewItem ItemDe(Archivo* a);
wxDataViewItem ItemDe(const Elemento& e);
Elemento ElementoDe(const wxDataViewItem& item);

// Íconos del sistema (los mismos del Explorador), guardados para no pedirlos cada vez
class Iconos
{
public:
    static Iconos& Todos();
    wxBitmapBundle DeCarpeta();
    wxBitmapBundle DeArchivo(const wchar_t* nombre);
    wxBitmapBundle DeUnidad(const std::wstring& raiz);
    wxBitmapBundle Cuadro(const wxColour& color, int lado);

private:
    wxBitmapBundle DelSistema(const wchar_t* ruta, unsigned long atributos, bool soloAtributos);
    std::unordered_map<std::wstring, wxBitmapBundle> guardados;
    std::wstring extension;
};

// Barra horizontal con el porcentaje al lado. El valor es una fracción de 0 a 1 (negativa: no se dibuja).
class RenderBarra : public wxDataViewCustomRenderer
{
public:
    RenderBarra();
    bool SetValue(const wxVariant& valor) override;
    bool GetValue(wxVariant& valor) const override;
    bool Render(wxRect celda, wxDC* dc, int estado) override;
    wxSize GetSize() const override;

private:
    double fraccion = -1;
};

class ModeloArbol : public wxDataViewModel
{
public:
    // Cambia los datos y avisa a la lista (después ya se puede soltar el resultado anterior)
    void Usar(std::shared_ptr<Resultado> resultado);
    const std::shared_ptr<Resultado>& Datos() const { return datos; }

    void GetValue(wxVariant& valor, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant&, const wxDataViewItem&, unsigned int) override { return false; }
    bool GetAttr(const wxDataViewItem& item, unsigned int col, wxDataViewItemAttr& attr) const override;
    wxDataViewItem GetParent(const wxDataViewItem& item) const override;
    bool IsContainer(const wxDataViewItem& item) const override;
    bool HasContainerColumns(const wxDataViewItem&) const override { return true; }
    unsigned int GetChildren(const wxDataViewItem& item, wxDataViewItemArray& hijos) const override;
    int Compare(const wxDataViewItem& a, const wxDataViewItem& b, unsigned int col, bool ascendente) const override;

private:
    std::shared_ptr<Resultado> datos;
};
