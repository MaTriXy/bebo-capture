#include <streams.h>
#include "Capture.h"
#include "CaptureGuids.h"
#include "DibHelper.h"
#include "Logging.h"

/**********************************************
 *
 *  CGameCapture Class Parent
 *
 **********************************************/

CGameCapture::CGameCapture(IUnknown *pUnk, HRESULT *phr, GUID clsid, int captureType)
           : CSource(NAME("PushSourceDesktop Parent"), pUnk, clsid)
{
    // The pin magically adds itself to our pin array.
	// except its not an array since we just have one [?]
    m_pPin = new CPushPinDesktop(phr, this, captureType);

	if (phr)
	{
		if (m_pPin == NULL)
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}  
}


CGameCapture::~CGameCapture() // parent destructor
{
	// COM should call this when the refcount hits 0...
	// but somebody should make the refcount 0...
    delete m_pPin;
}


CUnknown * WINAPI CGameCapture::CreateInstance_Game(IUnknown *pUnk, HRESULT *phr)
{
	// the first entry point
	setupLogging();
    CGameCapture *pNewFilter = new CGameCapture(pUnk, phr, CLSID_PushSourceDesktop_Game, CAPTURE_INJECT);

	if (phr)
	{
		if (pNewFilter == NULL) 
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}
    return pNewFilter;
}

CUnknown * WINAPI CGameCapture::CreateInstance_Window(IUnknown *pUnk, HRESULT *phr)
{
	// the first entry point
	setupLogging();
    CGameCapture *pNewFilter = new CGameCapture(pUnk, phr, CLSID_PushSourceDesktop_Window, CAPTURE_GDI);

	if (phr)
	{
		if (pNewFilter == NULL) 
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}
    return pNewFilter;
}

CUnknown * WINAPI CGameCapture::CreateInstance_Desktop(IUnknown *pUnk, HRESULT *phr)
{
	// the first entry point
	setupLogging();
    CGameCapture *pNewFilter = new CGameCapture(pUnk, phr, CLSID_PushSourceDesktop_Desktop, CAPTURE_DESKTOP);

	if (phr)
	{
		if (pNewFilter == NULL) 
			*phr = E_OUTOFMEMORY;
		else
			*phr = S_OK;
	}
    return pNewFilter;
}

HRESULT CGameCapture::QueryInterface(REFIID riid, void **ppv)
{
    //Forward request for IAMStreamConfig & IKsPropertySet to the pin
    if(riid == _uuidof(IAMStreamConfig) || riid == _uuidof(IKsPropertySet)) {
        return m_paStreams[0]->QueryInterface(riid, ppv);
	}
    else {
        return CSource::QueryInterface(riid, ppv);
	}

}
