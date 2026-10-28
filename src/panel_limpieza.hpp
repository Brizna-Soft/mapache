#pragma once

// Pestaña «Limpieza»: muestra cuánto se puede liberar y limpia lo marcado, con confirmación.

#include <wx/scrolwin.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "limpieza.hpp"

class wxButton;
class wxCheckBox;
class wxStaticText;

class PanelLimpieza : public wxScrolledWindow
{
public:
    PanelLimpieza(wxWindow* padre, const std::wstring& carpetaPrueba);
    ~PanelLimpieza() override;

    void CalcularSiHaceFalta();
    void Detener();          // pide parar y espera
    void PedirCancelar();    // pide parar sin esperar (al cerrar la ventana)
    bool Ocupado() const { return trabajando; }

private:
    struct Fila
    {
        wxCheckBox* casilla = nullptr;
        wxStaticText* tamano = nullptr;
        wxStaticText* descripcion = nullptr;
    };

    void Calcular();
    void Limpiar();
    void Calculado(const std::vector<limpieza::Grupo>& nuevos);
    void Limpiado(const limpieza::Informe& informe);
    void MostrarGrupos();
    void ActualizarTotal();
    void Ocupar(bool si, const wxString& texto);
    void Esperar();
    void Largo(wxStaticText* control, const wxString& texto, int sangria);   // texto que se acomoda al ancho
    void Envolver();

    struct TextoLargo
    {
        wxStaticText* control;
        wxString texto;
        int sangria;
    };
    std::vector<TextoLargo> textosLargos;
    int anchoEnvuelto = 0;

    std::wstring prueba;
    std::vector<limpieza::Grupo> grupos;
    std::vector<Fila> filas;
    wxStaticText* total = nullptr;
    wxStaticText* estado = nullptr;
    wxButton* botonLimpiar = nullptr;
    wxButton* botonCalcular = nullptr;
    wxString mensajeFinal;

    std::thread hilo;
    std::atomic<bool> cancelar{ false };
    bool trabajando = false;
    bool calculado = false;
    bool detenido = false;   // la ventana se está cerrando: no se empieza nada nuevo
};
