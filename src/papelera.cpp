#include "papelera.hpp"

#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <initializer_list>

#include "util.hpp"

namespace
{
    // Vigila la operación: si Windows fuera a borrar algo para siempre en vez de enviarlo
    // a la Papelera, la cancela antes de que pase.
    class Vigilante : public IFileOperationProgressSink
    {
    public:
        bool borradoDefinitivoEvitado = false;

        // IUnknown
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (!ppv) return E_POINTER;
            if (riid == IID_IUnknown || riid == __uuidof(IFileOperationProgressSink))
            {
                *ppv = static_cast<IFileOperationProgressSink*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&referencias); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            ULONG r = InterlockedDecrement(&referencias);
            if (r == 0) delete this;
            return r;
        }

        // Lo que importa: antes de borrar cada cosa
        HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD banderas, IShellItem*) override
        {
            if (!(banderas & TSF_DELETE_RECYCLE_IF_POSSIBLE))
            {
                borradoDefinitivoEvitado = true;
                return E_ABORT;
            }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT, IShellItem*) override { return S_OK; }

        // El resto no se usa
        HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
        HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
        HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
        HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

    private:
        ~Vigilante() = default;
        LONG referencias = 1;
    };

    // Primer nombre después de la raíz: «C:\Windows\System32» -> «Windows»
    std::wstring PrimerNivel(const std::wstring& ruta, const std::wstring& raiz)
    {
        if (ruta.size() <= raiz.size()) return std::wstring();
        std::wstring resto = ruta.substr(raiz.size());
        size_t i = resto.find(L'\\');
        return i == std::wstring::npos ? resto : resto.substr(0, i);
    }

    bool IgualOContiene(const std::wstring& ruta, const std::wstring& carpeta)
    {
        // La ruta es la carpeta, o la ruta contiene a la carpeta (borrarla la borraría también)
        return !carpeta.empty() && (util::MismaRuta(ruta, carpeta) || util::EstaDentro(carpeta, ruta));
    }
}

std::wstring papelera::CarpetaConocida(REFGUID id)
{
    PWSTR texto = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &texto)) && texto) r = util::Normalizar(texto);
    CoTaskMemFree(texto);
    return r;
}

std::wstring papelera::Motivo(const std::wstring& rutaOriginal)
{
    const std::wstring ruta = util::Normalizar(rutaOriginal);
    const wchar_t* deSistema =
        L"Es parte de Windows o de los programas instalados. Borrarlo puede dañar Windows o tus programas.\n\n"
        L"Para quitar un programa usa «Aplicaciones instaladas» en la Configuración de Windows.";

    if (util::EsRaiz(ruta)) return L"Es la raíz de la unidad: ahí está todo lo demás.";

    // Lo que está en la raíz de cualquier unidad y es de Windows
    const std::wstring raiz = util::RaizDe(ruta);
    const std::wstring primero = PrimerNivel(ruta, raiz);
    if (util::IgualSinMayusculas(primero, L"Windows.old"))
        return L"Windows.old es la copia de tu Windows anterior. Para quitarla sin riesgo usa el «Liberador de espacio» "
               L"de Windows (hay un botón en la pestaña «Limpieza»).";
    if (!primero.empty() && primero[0] == L'$') return deSistema;
    for (const wchar_t* n : { L"Windows", L"Program Files", L"Program Files (x86)", L"ProgramData", L"System Volume Information",
                              L"Recovery", L"Boot", L"EFI", L"Config.Msi", L"Documents and Settings", L"pagefile.sys",
                              L"hiberfil.sys", L"swapfile.sys", L"bootmgr", L"BOOTNXT", L"DumpStack.log", L"DumpStack.log.tmp" })
        if (util::IgualSinMayusculas(primero, n)) return deSistema;

    // Las mismas carpetas, estén donde estén
    for (REFGUID id : { FOLDERID_Windows, FOLDERID_ProgramFiles, FOLDERID_ProgramFilesX86, FOLDERID_ProgramData })
    {
        std::wstring c = CarpetaConocida(id);
        if (IgualOContiene(ruta, c) || util::EstaDentro(ruta, c)) return deSistema;
    }

    // Carpetas de las personas
    const std::wstring usuarios = CarpetaConocida(FOLDERID_UserProfiles);
    const std::wstring perfil = CarpetaConocida(FOLDERID_Profile);
    if (IgualOContiene(ruta, usuarios)) return L"Ahí están las carpetas de todas las personas que usan esta PC.";
    if (IgualOContiene(ruta, perfil))
        return L"Es tu carpeta personal entera (Documentos, Escritorio, Descargas y demás). Elige solo lo que de verdad quieres borrar.";
    if (util::EstaDentro(ruta, usuarios) && !util::EstaDentro(ruta, perfil))
        return L"Es de otra cuenta de esta PC o de Windows.";

    for (REFGUID id : { FOLDERID_Desktop, FOLDERID_Documents, FOLDERID_Downloads, FOLDERID_Music, FOLDERID_Pictures,
                        FOLDERID_Videos, FOLDERID_RoamingAppData, FOLDERID_LocalAppData, FOLDERID_LocalAppDataLow,
                        FOLDERID_SkyDrive })
    {
        if (IgualOContiene(ruta, CarpetaConocida(id)))
            return L"Es una carpeta especial de Windows (como «Documentos», «Escritorio» o «AppData»). Puedes borrar "
                   L"lo que tiene dentro, pero no la carpeta entera.";
    }

    // Sin Papelera, lo que se borra se pierde para siempre
    if (GetDriveTypeW(raiz.c_str()) != DRIVE_FIXED)
        return L"Esta unidad no tiene Papelera: lo que se borra ahí no se puede recuperar. Por seguridad, "
               L"Mapache no borra nada en ella.";

    return std::wstring();
}

papelera::Respuesta papelera::Enviar(HWND ventana, const std::wstring& ruta, std::wstring& detalle)
{
    detalle.clear();
    IFileOperation* operacion = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operacion))))
    {
        detalle = L"Windows no dejó preparar la operación. No se borró nada.";
        return Respuesta::NoSePudo;
    }
    operacion->SetOwnerWindow(ventana);
    // A la Papelera, sin preguntar otra vez (ya se preguntó), sin ventanas de error ni de permisos de
    // administrador, y avisando si Windows quisiera borrar para siempre.
    operacion->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_WANTNUKEWARNING
                                 | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE);

    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(ruta.c_str(), nullptr, IID_PPV_ARGS(&item))))
    {
        operacion->Release();
        detalle = L"Windows no encontró el elemento (quizás ya no existe o su ruta es demasiado larga). No se borró nada.";
        return Respuesta::NoSePudo;
    }

    auto* vigilante = new Vigilante();
    DWORD cookie = 0;
    bool vigilado = SUCCEEDED(operacion->Advise(vigilante, &cookie));
    // Sin vigilante no se hace nada: es lo que garantiza que nada se borre para siempre
    if (vigilado && SUCCEEDED(operacion->DeleteItem(item, nullptr))) operacion->PerformOperations();
    if (vigilado) operacion->Unadvise(cookie);
    bool definitivoEvitado = vigilante->borradoDefinitivoEvitado;
    vigilante->Release();
    item->Release();
    operacion->Release();

    if (!util::Existe(ruta)) return Respuesta::Enviado;
    if (definitivoEvitado)
        detalle = L"Windows lo iba a borrar para siempre (por ejemplo, porque no cabe en la Papelera), así que se canceló. "
                  L"No se borró nada.";
    else
        detalle = L"Puede que esté abierto en otro programa o que no tengas permiso. No se borró nada.";
    return Respuesta::NoSePudo;
}
