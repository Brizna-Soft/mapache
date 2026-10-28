#pragma once

// Mapa de rectángulos (treemap): cada archivo es un rectángulo de tamaño proporcional a lo que ocupa,
// con el color de su tipo. Las carpetas agrupan lo que tienen dentro.

#include <wx/bitmap.h>
#include <wx/window.h>

#include <functional>
#include <memory>
#include <vector>

#include "analisis.hpp"

class MapaPanel : public wxWindow
{
public:
    explicit MapaPanel(wxWindow* padre);

    void Mostrar(std::shared_ptr<Resultado> resultado, Carpeta* carpeta);
    void Rehacer();   // algo cambió (por ejemplo, se envió a la Papelera)
    void Vaciar();
    Carpeta* Actual() const { return actual; }

    std::function<void(const Elemento&)> alSenalar;   // el ratón pasa por encima o se hace clic
    std::function<void(Carpeta*)> alEntrar;           // doble clic: mostrar esa carpeta
    std::function<void(const Elemento&)> alMenu;      // clic derecho

private:
    struct Caja
    {
        double x, y, w, h;
        Elemento e;
        util::Categoria categoria;
        int nivel;
    };

    void Distribuir();
    void DistribuirEn(Carpeta* c, double x, double y, double w, double h, int nivel);
    void Dibujar();
    int CajaEn(const wxPoint& p) const;
    static wxRect Redondear(const Caja& c);

    std::shared_ptr<Resultado> resultado;
    Carpeta* actual = nullptr;
    std::vector<Caja> cajas;
    wxBitmap lienzo;
    bool sucio = true;
    int elegida = -1;
    int senalada = -1;
};
