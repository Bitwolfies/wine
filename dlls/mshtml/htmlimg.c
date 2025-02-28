/*
 * Copyright 2008 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "mshtmdid.h"

#include "wine/debug.h"

#include "mshtml_private.h"
#include "htmlevent.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

struct HTMLImg {
    HTMLElement element;

    IHTMLImgElement IHTMLImgElement_iface;

    nsIDOMHTMLImageElement *nsimg;
    eventid_t skip_event;
};

static inline HTMLImg *impl_from_IHTMLImgElement(IHTMLImgElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLImg, IHTMLImgElement_iface);
}

static HRESULT WINAPI HTMLImgElement_QueryInterface(IHTMLImgElement *iface, REFIID riid, void **ppv)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    return IHTMLDOMNode_QueryInterface(&This->element.node.IHTMLDOMNode_iface, riid, ppv);
}

static ULONG WINAPI HTMLImgElement_AddRef(IHTMLImgElement *iface)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    return IHTMLDOMNode_AddRef(&This->element.node.IHTMLDOMNode_iface);
}

static ULONG WINAPI HTMLImgElement_Release(IHTMLImgElement *iface)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    return IHTMLDOMNode_Release(&This->element.node.IHTMLDOMNode_iface);
}

static HRESULT WINAPI HTMLImgElement_GetTypeInfoCount(IHTMLImgElement *iface, UINT *pctinfo)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    return IDispatchEx_GetTypeInfoCount(&This->element.node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLImgElement_GetTypeInfo(IHTMLImgElement *iface, UINT iTInfo,
                                              LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    return IDispatchEx_GetTypeInfo(&This->element.node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid,
            ppTInfo);
}

static HRESULT WINAPI HTMLImgElement_GetIDsOfNames(IHTMLImgElement *iface, REFIID riid,
                                                LPOLESTR *rgszNames, UINT cNames,
                                                LCID lcid, DISPID *rgDispId)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    return IDispatchEx_GetIDsOfNames(&This->element.node.event_target.dispex.IDispatchEx_iface, riid, rgszNames,
            cNames, lcid, rgDispId);
}

static HRESULT WINAPI HTMLImgElement_Invoke(IHTMLImgElement *iface, DISPID dispIdMember,
                            REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                            VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    return IDispatchEx_Invoke(&This->element.node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid,
            lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLImgElement_put_isMap(IHTMLImgElement *iface, VARIANT_BOOL v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsresult nsres;

    TRACE("(%p)->(%x)\n", This, v);

    nsres = nsIDOMHTMLImageElement_SetIsMap(This->nsimg, v != VARIANT_FALSE);
    if (NS_FAILED(nsres)) {
        ERR("Set IsMap failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_get_isMap(IHTMLImgElement *iface, VARIANT_BOOL *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    cpp_bool b;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if (p == NULL)
        return E_INVALIDARG;

    nsres = nsIDOMHTMLImageElement_GetIsMap(This->nsimg, &b);
    if (NS_FAILED(nsres)) {
        ERR("Get IsMap failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = variant_bool(b);
    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_put_useMap(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_useMap(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_mimeType(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_fileSize(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_fileCreatedDate(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_fileModifiedDate(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_fileUpdatedDate(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_protocol(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_href(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_nameProp(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_put_border(IHTMLImgElement *iface, VARIANT v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_border(IHTMLImgElement *iface, VARIANT *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_put_vspace(IHTMLImgElement *iface, LONG v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_vspace(IHTMLImgElement *iface, LONG *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_put_hspace(IHTMLImgElement *iface, LONG v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_hspace(IHTMLImgElement *iface, LONG *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_put_alt(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsAString alt_str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&alt_str, v);
    nsres = nsIDOMHTMLImageElement_SetAlt(This->nsimg, &alt_str);
    nsAString_Finish(&alt_str);
    if(NS_FAILED(nsres))
        ERR("SetAlt failed: %08lx\n", nsres);

    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_get_alt(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsAString alt_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&alt_str, NULL);
    nsres = nsIDOMHTMLImageElement_GetAlt(This->nsimg, &alt_str);
    return return_nsstr(nsres, &alt_str, p);
}

static HRESULT WINAPI HTMLImgElement_put_src(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    HRESULT hres = S_OK;
    nsAString src_str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&src_str, v);
    nsres = nsIDOMHTMLImageElement_SetSrc(This->nsimg, &src_str);
    nsAString_Finish(&src_str);
    if(NS_FAILED(nsres))
        ERR("SetSrc failed: %08lx\n", nsres);

    if(dispex_compat_mode(&This->element.node.event_target.dispex) < COMPAT_MODE_IE9) {
        eventid_t eventid;
        cpp_bool complete;
        UINT32 height = 0;
        DOMEvent *event;

        /* Synchronously send load event if the image was completed immediately (such as from cache) */
        This->skip_event = EVENTID_INVALID_ID;

        nsres = nsIDOMHTMLImageElement_GetComplete(This->nsimg, &complete);
        if(NS_SUCCEEDED(nsres) && complete) {
            nsIDOMHTMLImageElement_GetNaturalHeight(This->nsimg, &height);
            eventid = height ? EVENTID_LOAD : EVENTID_ERROR;

            hres = create_document_event(This->element.node.doc, eventid, &event);
            if(SUCCEEDED(hres)) {
                This->skip_event = eventid;
                dispatch_event(&This->element.node.event_target, event);
                IDOMEvent_Release(&event->IDOMEvent_iface);
            }
        }
    }
    return hres;
}

static HRESULT WINAPI HTMLImgElement_get_src(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    const PRUnichar *src;
    nsAString src_str;
    nsresult nsres;
    HRESULT hres = S_OK;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&src_str, NULL);
    nsres = nsIDOMHTMLImageElement_GetSrc(This->nsimg, &src_str);
    if(NS_SUCCEEDED(nsres)) {
        nsAString_GetData(&src_str, &src);

        if(!wcsnicmp(src, L"BLOCKED::", ARRAY_SIZE(L"BLOCKED::")-1)) {
            TRACE("returning BLOCKED::\n");
            *p = SysAllocString(L"BLOCKED::");
            if(!*p)
                hres = E_OUTOFMEMORY;
        }else {
            hres = nsuri_to_url(src, TRUE, p);
        }
    }else {
        ERR("GetSrc failed: %08lx\n", nsres);
        hres = E_FAIL;
    }

    nsAString_Finish(&src_str);
    return hres;
}

static HRESULT WINAPI HTMLImgElement_put_lowsrc(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_lowsrc(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_put_vrml(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_vrml(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_put_dynsrc(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_dynsrc(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_readyState(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_complete(IHTMLImgElement *iface, VARIANT_BOOL *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    cpp_bool complete;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLImageElement_GetComplete(This->nsimg, &complete);
    if(NS_FAILED(nsres)) {
        ERR("GetComplete failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = variant_bool(complete);
    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_put_loop(IHTMLImgElement *iface, VARIANT v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_loop(IHTMLImgElement *iface, VARIANT *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_put_align(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    nsAString_InitDepend(&str, v);

    nsres = nsIDOMHTMLImageElement_SetAlign(This->nsimg, &str);
    nsAString_Finish(&str);
    if (NS_FAILED(nsres)){
        ERR("Set Align(%s) failed: %08lx\n", debugstr_w(v), nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_get_align(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsAString str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&str, NULL);
    nsres = nsIDOMHTMLImageElement_GetAlign(This->nsimg, &str);

    return return_nsstr(nsres, &str, p);
}

static HRESULT WINAPI HTMLImgElement_put_onload(IHTMLImgElement *iface, VARIANT v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->element.node, EVENTID_LOAD, &v);
}

static HRESULT WINAPI HTMLImgElement_get_onload(IHTMLImgElement *iface, VARIANT *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_LOAD, p);
}

static HRESULT WINAPI HTMLImgElement_put_onerror(IHTMLImgElement *iface, VARIANT v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    TRACE("(%p)->()\n", This);

    return set_node_event(&This->element.node, EVENTID_ERROR, &v);
}

static HRESULT WINAPI HTMLImgElement_get_onerror(IHTMLImgElement *iface, VARIANT *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_ERROR, p);
}

static HRESULT WINAPI HTMLImgElement_put_onabort(IHTMLImgElement *iface, VARIANT v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    TRACE("(%p)->()\n", This);

    return set_node_event(&This->element.node, EVENTID_ABORT, &v);
}

static HRESULT WINAPI HTMLImgElement_get_onabort(IHTMLImgElement *iface, VARIANT *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->element.node, EVENTID_ABORT, p);
}

static HRESULT WINAPI HTMLImgElement_put_name(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_name(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsAString name;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsAString_Init(&name, NULL);
    nsres = nsIDOMHTMLImageElement_GetName(This->nsimg, &name);
    return return_nsstr(nsres, &name, p);
}

static HRESULT WINAPI HTMLImgElement_put_width(IHTMLImgElement *iface, LONG v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsresult nsres;

    TRACE("(%p)->(%ld)\n", This, v);

    nsres = nsIDOMHTMLImageElement_SetWidth(This->nsimg, v);
    if(NS_FAILED(nsres)) {
        ERR("SetWidth failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_get_width(IHTMLImgElement *iface, LONG *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    UINT32 width;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLImageElement_GetWidth(This->nsimg, &width);
    if(NS_FAILED(nsres)) {
        ERR("GetWidth failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = width;
    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_put_height(IHTMLImgElement *iface, LONG v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    nsresult nsres;

    TRACE("(%p)->(%ld)\n", This, v);

    nsres = nsIDOMHTMLImageElement_SetHeight(This->nsimg, v);
    if(NS_FAILED(nsres)) {
        ERR("SetHeight failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_get_height(IHTMLImgElement *iface, LONG *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    UINT32 height;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMHTMLImageElement_GetHeight(This->nsimg, &height);
    if(NS_FAILED(nsres)) {
        ERR("GetHeight failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = height;
    return S_OK;
}

static HRESULT WINAPI HTMLImgElement_put_start(IHTMLImgElement *iface, BSTR v)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLImgElement_get_start(IHTMLImgElement *iface, BSTR *p)
{
    HTMLImg *This = impl_from_IHTMLImgElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static const IHTMLImgElementVtbl HTMLImgElementVtbl = {
    HTMLImgElement_QueryInterface,
    HTMLImgElement_AddRef,
    HTMLImgElement_Release,
    HTMLImgElement_GetTypeInfoCount,
    HTMLImgElement_GetTypeInfo,
    HTMLImgElement_GetIDsOfNames,
    HTMLImgElement_Invoke,
    HTMLImgElement_put_isMap,
    HTMLImgElement_get_isMap,
    HTMLImgElement_put_useMap,
    HTMLImgElement_get_useMap,
    HTMLImgElement_get_mimeType,
    HTMLImgElement_get_fileSize,
    HTMLImgElement_get_fileCreatedDate,
    HTMLImgElement_get_fileModifiedDate,
    HTMLImgElement_get_fileUpdatedDate,
    HTMLImgElement_get_protocol,
    HTMLImgElement_get_href,
    HTMLImgElement_get_nameProp,
    HTMLImgElement_put_border,
    HTMLImgElement_get_border,
    HTMLImgElement_put_vspace,
    HTMLImgElement_get_vspace,
    HTMLImgElement_put_hspace,
    HTMLImgElement_get_hspace,
    HTMLImgElement_put_alt,
    HTMLImgElement_get_alt,
    HTMLImgElement_put_src,
    HTMLImgElement_get_src,
    HTMLImgElement_put_lowsrc,
    HTMLImgElement_get_lowsrc,
    HTMLImgElement_put_vrml,
    HTMLImgElement_get_vrml,
    HTMLImgElement_put_dynsrc,
    HTMLImgElement_get_dynsrc,
    HTMLImgElement_get_readyState,
    HTMLImgElement_get_complete,
    HTMLImgElement_put_loop,
    HTMLImgElement_get_loop,
    HTMLImgElement_put_align,
    HTMLImgElement_get_align,
    HTMLImgElement_put_onload,
    HTMLImgElement_get_onload,
    HTMLImgElement_put_onerror,
    HTMLImgElement_get_onerror,
    HTMLImgElement_put_onabort,
    HTMLImgElement_get_onabort,
    HTMLImgElement_put_name,
    HTMLImgElement_get_name,
    HTMLImgElement_put_width,
    HTMLImgElement_get_width,
    HTMLImgElement_put_height,
    HTMLImgElement_get_height,
    HTMLImgElement_put_start,
    HTMLImgElement_get_start
};

static inline HTMLImg *impl_from_HTMLDOMNode(HTMLDOMNode *iface)
{
    return CONTAINING_RECORD(iface, HTMLImg, element.node);
}

static HRESULT HTMLImgElement_QI(HTMLDOMNode *iface, REFIID riid, void **ppv)
{
    HTMLImg *This = impl_from_HTMLDOMNode(iface);

    *ppv = NULL;

    if(IsEqualGUID(&IID_IHTMLImgElement, riid)) {
        TRACE("(%p)->(IID_IHTMLImgElement %p)\n", This, ppv);
        *ppv = &This->IHTMLImgElement_iface;
    }else {
        return HTMLElement_QI(&This->element.node, riid, ppv);
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static HRESULT HTMLImgElement_dispatch_nsevent_hook(HTMLDOMNode *iface, DOMEvent *event)
{
    HTMLImg *This = impl_from_HTMLDOMNode(iface);

    if(event->event_id == This->skip_event) {
        This->skip_event = EVENTID_INVALID_ID;
        return S_OK;
    }

    return S_FALSE;
}

static HRESULT HTMLImgElement_get_readystate(HTMLDOMNode *iface, BSTR *p)
{
    HTMLImg *This = impl_from_HTMLDOMNode(iface);

    return IHTMLImgElement_get_readyState(&This->IHTMLImgElement_iface, p);
}

static void HTMLImgElement_traverse(HTMLDOMNode *iface, nsCycleCollectionTraversalCallback *cb)
{
    HTMLImg *This = impl_from_HTMLDOMNode(iface);

    if(This->nsimg)
        note_cc_edge((nsISupports*)This->nsimg, "This->nsimg", cb);
}

static void HTMLImgElement_unlink(HTMLDOMNode *iface)
{
    HTMLImg *This = impl_from_HTMLDOMNode(iface);

    if(This->nsimg) {
        nsIDOMHTMLImageElement *nsimg = This->nsimg;

        This->nsimg = NULL;
        nsIDOMHTMLImageElement_Release(nsimg);
    }
}

static const NodeImplVtbl HTMLImgElementImplVtbl = {
    &CLSID_HTMLImg,
    HTMLImgElement_QI,
    HTMLElement_destructor,
    HTMLElement_cpc,
    HTMLElement_clone,
    HTMLImgElement_dispatch_nsevent_hook,
    HTMLElement_handle_event,
    HTMLElement_get_attr_col,
    NULL,
    NULL,
    NULL,
    NULL,
    HTMLImgElement_get_readystate,
    NULL,
    NULL,
    NULL,
    NULL,
    HTMLImgElement_traverse,
    HTMLImgElement_unlink
};

static const tid_t HTMLImgElement_iface_tids[] = {
    HTMLELEMENT_TIDS,
    0
};

static void HTMLImgElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t img_ie11_hooks[] = {
        {DISPID_IHTMLIMGELEMENT_FILESIZE, NULL},
        {DISPID_UNKNOWN}
    };

    HTMLElement_init_dispex_info(info, mode);

    dispex_info_add_interface(info, IHTMLImgElement_tid, mode >= COMPAT_MODE_IE11 ? img_ie11_hooks : NULL);
}

dispex_static_data_t HTMLImgElement_dispex = {
    L"HTMLImageElement",
    NULL,
    PROTO_ID_HTMLImgElement,
    DispHTMLImg_tid,
    HTMLImgElement_iface_tids,
    HTMLImgElement_init_dispex_info
};

HRESULT HTMLImgElement_Create(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLImg *ret;
    nsresult nsres;

    ret = calloc(1, sizeof(HTMLImg));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IHTMLImgElement_iface.lpVtbl = &HTMLImgElementVtbl;
    ret->element.node.vtbl = &HTMLImgElementImplVtbl;
    ret->skip_event = EVENTID_INVALID_ID;

    HTMLElement_Init(&ret->element, doc, nselem, &HTMLImgElement_dispex);

    nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMHTMLImageElement, (void**)&ret->nsimg);
    assert(nsres == NS_OK);

    *elem = &ret->element;
    return S_OK;
}

static inline struct legacy_ctor *impl_from_IHTMLImageElementFactory(IHTMLImageElementFactory *iface)
{
    return CONTAINING_RECORD(iface, struct legacy_ctor, IHTMLImageElementFactory_iface);
}

static HRESULT WINAPI HTMLImageElementFactory_QueryInterface(IHTMLImageElementFactory *iface,
        REFIID riid, void **ppv)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        *ppv = &This->IHTMLImageElementFactory_iface;
    }else if(IsEqualGUID(&IID_IHTMLImageElementFactory, riid)) {
        *ppv = &This->IHTMLImageElementFactory_iface;
    }else if(dispex_query_interface(&This->dispex, riid, ppv)) {
        return *ppv ? S_OK : E_NOINTERFACE;
    }else {
        *ppv = NULL;
        WARN("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI HTMLImageElementFactory_AddRef(IHTMLImageElementFactory *iface)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI HTMLImageElementFactory_Release(IHTMLImageElementFactory *iface)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
        /* Proxy constructor disps hold ref to window, others are always detached first */
        if(This->window)
            IHTMLWindow2_Release(&This->window->base.IHTMLWindow2_iface);
        release_dispex(&This->dispex);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI HTMLImageElementFactory_GetTypeInfoCount(IHTMLImageElementFactory *iface,
        UINT *pctinfo)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);
    return IDispatchEx_GetTypeInfoCount(&This->dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLImageElementFactory_GetTypeInfo(IHTMLImageElementFactory *iface,
        UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);
    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLImageElementFactory_GetIDsOfNames(IHTMLImageElementFactory *iface,
        REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid,
        DISPID *rgDispId)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);
    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames, lcid, rgDispId);
}

static HRESULT WINAPI HTMLImageElementFactory_Invoke(IHTMLImageElementFactory *iface,
        DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
        DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo,
        UINT *puArgErr)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);
    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid, wFlags,
            pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static LONG var_to_size(const VARIANT *v)
{
    switch(V_VT(v)) {
    case VT_EMPTY:
        return 0;
    case VT_I4:
        return V_I4(v);
    case VT_BSTR: {
        LONG ret;
        HRESULT hres;

        hres = VarI4FromStr(V_BSTR(v), 0, 0, &ret);
        if(FAILED(hres)) {
            FIXME("VarI4FromStr failed: %08lx\n", hres);
            return 0;
        }
        return ret;
    }
    default:
        FIXME("unsupported size %s\n", debugstr_variant(v));
    }
    return 0;
}

static HRESULT WINAPI HTMLImageElementFactory_create(IHTMLImageElementFactory *iface,
        VARIANT width, VARIANT height, IHTMLImgElement **img_elem)
{
    struct legacy_ctor *This = impl_from_IHTMLImageElementFactory(iface);
    HTMLDocumentNode *doc;
    IHTMLImgElement *img;
    HTMLElement *elem;
    nsIDOMElement *nselem;
    LONG l;
    HRESULT hres;

    TRACE("(%p)->(%s %s %p)\n", This, debugstr_variant(&width),
            debugstr_variant(&height), img_elem);

    if(!This->window || !This->window->doc) {
        WARN("NULL doc\n");
        return E_UNEXPECTED;
    }

    doc = This->window->doc;

    *img_elem = NULL;

    hres = create_nselem(doc, L"IMG", &nselem);
    if(FAILED(hres))
        return hres;

    hres = HTMLElement_Create(doc, (nsIDOMNode*)nselem, FALSE, &elem);
    nsIDOMElement_Release(nselem);
    if(FAILED(hres)) {
        ERR("HTMLElement_Create failed\n");
        return hres;
    }

    hres = IHTMLElement_QueryInterface(&elem->IHTMLElement_iface, &IID_IHTMLImgElement,
            (void**)&img);
    IHTMLElement_Release(&elem->IHTMLElement_iface);
    if(FAILED(hres)) {
        ERR("IHTMLElement_QueryInterface failed: 0x%08lx\n", hres);
        return hres;
    }

    l = var_to_size(&width);
    if(l)
        IHTMLImgElement_put_width(img, l);
    l = var_to_size(&height);
    if(l)
        IHTMLImgElement_put_height(img, l);

    *img_elem = img;
    return S_OK;
}

const IHTMLImageElementFactoryVtbl HTMLImageElementFactoryVtbl = {
    HTMLImageElementFactory_QueryInterface,
    HTMLImageElementFactory_AddRef,
    HTMLImageElementFactory_Release,
    HTMLImageElementFactory_GetTypeInfoCount,
    HTMLImageElementFactory_GetTypeInfo,
    HTMLImageElementFactory_GetIDsOfNames,
    HTMLImageElementFactory_Invoke,
    HTMLImageElementFactory_create
};

static inline struct legacy_ctor *impl_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, struct legacy_ctor, dispex);
}

static HRESULT HTMLImageElementFactory_value(DispatchEx *dispex, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *res, EXCEPINFO *ei,
        IServiceProvider *caller)
{
    struct legacy_ctor *This = impl_from_DispatchEx(dispex);
    IHTMLImgElement *img;
    VARIANT empty, *width, *height;
    HRESULT hres;
    int argc = params->cArgs - params->cNamedArgs;

    if(flags != DISPATCH_CONSTRUCT)
        return S_FALSE;

    V_VT(res) = VT_NULL;

    V_VT(&empty) = VT_EMPTY;

    width = argc >= 1 ? params->rgvarg + (params->cArgs - 1) : &empty;
    height = argc >= 2 ? params->rgvarg + (params->cArgs - 2) : &empty;

    hres = IHTMLImageElementFactory_create(&This->IHTMLImageElementFactory_iface, *width, *height,
            &img);
    if(FAILED(hres))
        return hres;

    V_VT(res) = VT_DISPATCH;
    V_DISPATCH(res) = (IDispatch*)img;

    return S_OK;
}

static const dispex_static_data_vtbl_t HTMLImageElementFactory_dispex_vtbl = {
    HTMLImageElementFactory_value,
    legacy_ctor_get_dispid,
    legacy_ctor_get_name,
    legacy_ctor_invoke,
    legacy_ctor_delete
};

static const tid_t HTMLImageElementFactory_iface_tids[] = {
    IHTMLImageElementFactory_tid,
    0
};

dispex_static_data_t HTMLImageElementFactory_dispex = {
    L"HTMLImageElement",
    &HTMLImageElementFactory_dispex_vtbl,
    PROTO_ID_NULL,
    IHTMLImageElementFactory_tid,
    HTMLImageElementFactory_iface_tids
};

static HRESULT HTMLImageCtor_value(DispatchEx *iface, LCID lcid, WORD flags, DISPPARAMS *params,
        VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    if(flags == DISPATCH_CONSTRUCT)
        return HTMLImageElementFactory_value(iface, lcid, flags, params, res, ei, caller);

    return legacy_ctor_value(iface, lcid, flags, params, res, ei, caller);
}

static void HTMLImageCtor_init_dispex_info(dispex_data_t *info, compat_mode_t compat_mode)
{
    if(compat_mode < COMPAT_MODE_IE9)
        dispex_info_add_interface(info, IHTMLImageElementFactory_tid, NULL);
}

static const dispex_static_data_vtbl_t HTMLImageCtor_dispex_vtbl = {
    HTMLImageCtor_value,
    legacy_ctor_get_dispid,
    legacy_ctor_get_name,
    legacy_ctor_invoke,
    legacy_ctor_delete
};

dispex_static_data_t HTMLImageCtor_dispex = {
    L"HTMLImageElement",
    &HTMLImageCtor_dispex_vtbl,
    PROTO_ID_NULL,
    IHTMLImageElementFactory_tid,
    no_iface_tids,
    HTMLImageCtor_init_dispex_info
};
