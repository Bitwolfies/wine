/*
 * Copyright 2006-2010 Jacek Caban for CodeWeavers
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
#include <math.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "ole2.h"
#include "shlwapi.h"
#include "mshtmdid.h"

#include "wine/debug.h"

#include "mshtml_private.h"
#include "htmlevent.h"
#include "htmlstyle.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

#define ATTRFLAG_CASESENSITIVE  0x0001
#define ATTRFLAG_ASSTRING       0x0002
#define ATTRFLAG_EXPANDURL      0x0004

typedef struct {
    const WCHAR *name;
    HRESULT (*constructor)(HTMLDocumentNode*,nsIDOMElement*,HTMLElement**);
} tag_desc_t;

static HRESULT HTMLElement_Ctor(HTMLDocumentNode*,nsIDOMElement*,HTMLElement**);

static const tag_desc_t tag_descs[] = {
    {L"A",              HTMLAnchorElement_Create},
    {L"ABBR",           HTMLElement_Ctor},
    {L"ACRONYM",        HTMLElement_Ctor},
    {L"ADDRESS",        HTMLElement_Ctor},
    {L"APPLET",         HTMLElement_Ctor},
    {L"AREA",           HTMLAreaElement_Create},
    {L"ARTICLE",        HTMLElement_Ctor},
    {L"ASIDE",          HTMLElement_Ctor},
    {L"AUDIO",          HTMLElement_Ctor},
    {L"B",              HTMLElement_Ctor},
    {L"BASE",           HTMLElement_Ctor},
    {L"BASEFONT",       HTMLElement_Ctor},
    {L"BDO",            HTMLElement_Ctor},
    {L"BIG",            HTMLElement_Ctor},
    {L"BLOCKQUOTE",     HTMLElement_Ctor},
    {L"BODY",           HTMLBodyElement_Create},
    {L"BR",             HTMLElement_Ctor},
    {L"BUTTON",         HTMLButtonElement_Create},
    {L"CANVAS",         HTMLElement_Ctor},
    {L"CAPTION",        HTMLElement_Ctor},
    {L"CENTER",         HTMLElement_Ctor},
    {L"CITE",           HTMLElement_Ctor},
    {L"CODE",           HTMLElement_Ctor},
    {L"COL",            HTMLElement_Ctor},
    {L"COLGROUP",       HTMLElement_Ctor},
    {L"DATALIST",       HTMLElement_Ctor},
    {L"DD",             HTMLElement_Ctor},
    {L"DEL",            HTMLElement_Ctor},
    {L"DFN",            HTMLElement_Ctor},
    {L"DIR",            HTMLElement_Ctor},
    {L"DIV",            HTMLElement_Ctor},
    {L"DL",             HTMLElement_Ctor},
    {L"DT",             HTMLElement_Ctor},
    {L"EM",             HTMLElement_Ctor},
    {L"EMBED",          HTMLEmbedElement_Create},
    {L"FIELDSET",       HTMLElement_Ctor},
    {L"FIGCAPTION",     HTMLElement_Ctor},
    {L"FIGURE",         HTMLElement_Ctor},
    {L"FONT",           HTMLElement_Ctor},
    {L"FOOTER",         HTMLElement_Ctor},
    {L"FORM",           HTMLFormElement_Create},
    {L"FRAME",          HTMLFrameElement_Create},
    {L"FRAMESET",       HTMLElement_Ctor},
    {L"H1",             HTMLElement_Ctor},
    {L"H2",             HTMLElement_Ctor},
    {L"H3",             HTMLElement_Ctor},
    {L"H4",             HTMLElement_Ctor},
    {L"H5",             HTMLElement_Ctor},
    {L"H6",             HTMLElement_Ctor},
    {L"HEAD",           HTMLHeadElement_Create},
    {L"HEADER",         HTMLElement_Ctor},
    {L"HR",             HTMLElement_Ctor},
    {L"HTML",           HTMLHtmlElement_Create},
    {L"I",              HTMLElement_Ctor},
    {L"IFRAME",         HTMLIFrame_Create},
    {L"IMG",            HTMLImgElement_Create},
    {L"INPUT",          HTMLInputElement_Create},
    {L"INS",            HTMLElement_Ctor},
    {L"KBD",            HTMLElement_Ctor},
    {L"LABEL",          HTMLLabelElement_Create},
    {L"LEGEND",         HTMLElement_Ctor},
    {L"LI",             HTMLElement_Ctor},
    {L"LINK",           HTMLLinkElement_Create},
    {L"MAP",            HTMLElement_Ctor},
    {L"MARK",           HTMLElement_Ctor},
    {L"META",           HTMLMetaElement_Create},
    {L"NAV",            HTMLElement_Ctor},
    {L"NOFRAMES",       HTMLElement_Ctor},
    {L"NOSCRIPT",       HTMLElement_Ctor},
    {L"OBJECT",         HTMLObjectElement_Create},
    {L"OL",             HTMLElement_Ctor},
    {L"OPTGROUP",       HTMLElement_Ctor},
    {L"OPTION",         HTMLOptionElement_Create},
    {L"P",              HTMLElement_Ctor},
    {L"PARAM",          HTMLElement_Ctor},
    {L"PRE",            HTMLElement_Ctor},
    {L"PROGRESS",       HTMLElement_Ctor},
    {L"Q",              HTMLElement_Ctor},
    {L"RP",             HTMLElement_Ctor},
    {L"RT",             HTMLElement_Ctor},
    {L"RUBY",           HTMLElement_Ctor},
    {L"S",              HTMLElement_Ctor},
    {L"SAMP",           HTMLElement_Ctor},
    {L"SCRIPT",         HTMLScriptElement_Create},
    {L"SECTION",        HTMLElement_Ctor},
    {L"SELECT",         HTMLSelectElement_Create},
    {L"SMALL",          HTMLElement_Ctor},
    {L"SOURCE",         HTMLElement_Ctor},
    {L"SPAN",           HTMLElement_Ctor},
    {L"STRIKE",         HTMLElement_Ctor},
    {L"STRONG",         HTMLElement_Ctor},
    {L"STYLE",          HTMLStyleElement_Create},
    {L"SUB",            HTMLElement_Ctor},
    {L"SUP",            HTMLElement_Ctor},
    {L"TABLE",          HTMLTable_Create},
    {L"TBODY",          HTMLElement_Ctor},
    {L"TD",             HTMLTableCell_Create},
    {L"TEXTAREA",       HTMLTextAreaElement_Create},
    {L"TFOOT",          HTMLElement_Ctor},
    {L"TH",             HTMLElement_Ctor},
    {L"THEAD",          HTMLElement_Ctor},
    {L"TITLE",          HTMLTitleElement_Create},
    {L"TR",             HTMLTableRow_Create},
    {L"TRACK",          HTMLElement_Ctor},
    {L"TT",             HTMLElement_Ctor},
    {L"U",              HTMLElement_Ctor},
    {L"UL",             HTMLElement_Ctor},
    {L"VAR",            HTMLElement_Ctor},
    {L"VIDEO",          HTMLElement_Ctor},
    {L"WBR",            HTMLElement_Ctor}
};

static const tag_desc_t *get_tag_desc(const WCHAR *tag_name)
{
    DWORD min=0, max=ARRAY_SIZE(tag_descs)-1, i;
    int r;

    while(min <= max) {
        i = (min+max)/2;
        r = wcscmp(tag_name, tag_descs[i].name);
        if(!r)
            return tag_descs+i;

        if(r < 0)
            max = i-1;
        else
            min = i+1;
    }

    return NULL;
}

HRESULT replace_node_by_html(nsIDOMDocument *nsdoc, nsIDOMNode *nsnode, const WCHAR *html)
{
    nsIDOMDocumentFragment *nsfragment;
    nsIDOMNode *nsparent;
    nsIDOMRange *range;
    nsAString html_str;
    nsresult nsres;
    HRESULT hres = S_OK;

    nsres = nsIDOMDocument_CreateRange(nsdoc, &range);
    if(NS_FAILED(nsres)) {
        ERR("CreateRange failed: %08lx\n", nsres);
        return E_FAIL;
    }

    nsAString_InitDepend(&html_str, html);
    nsIDOMRange_CreateContextualFragment(range, &html_str, &nsfragment);
    nsIDOMRange_Release(range);
    nsAString_Finish(&html_str);
    if(NS_FAILED(nsres)) {
        ERR("CreateContextualFragment failed: %08lx\n", nsres);
        return E_FAIL;
    }

    nsres = nsIDOMNode_GetParentNode(nsnode, &nsparent);
    if(NS_SUCCEEDED(nsres) && nsparent) {
        nsIDOMNode *nstmp;

        nsres = nsIDOMNode_ReplaceChild(nsparent, (nsIDOMNode*)nsfragment, nsnode, &nstmp);
        nsIDOMNode_Release(nsparent);
        if(NS_FAILED(nsres)) {
            ERR("ReplaceChild failed: %08lx\n", nsres);
            hres = E_FAIL;
        }else if(nstmp) {
            nsIDOMNode_Release(nstmp);
        }
    }else {
        ERR("GetParentNode failed: %08lx\n", nsres);
        hres = E_FAIL;
    }

    nsIDOMDocumentFragment_Release(nsfragment);
    return hres;
}

nsresult get_elem_attr_value(nsIDOMElement *nselem, const WCHAR *name, nsAString *val_str, const PRUnichar **val)
{
    nsAString name_str;
    nsresult nsres;

    nsAString_InitDepend(&name_str, name);
    nsAString_Init(val_str, NULL);
    nsres = nsIDOMElement_GetAttribute(nselem, &name_str, val_str);
    nsAString_Finish(&name_str);
    if(NS_FAILED(nsres)) {
        ERR("GetAttribute(%s) failed: %08lx\n", debugstr_w(name), nsres);
        nsAString_Finish(val_str);
        return nsres;
    }

    nsAString_GetData(val_str, val);
    return NS_OK;
}

HRESULT elem_string_attr_getter(HTMLElement *elem, const WCHAR *name, BOOL use_null, BSTR *p)
{
    const PRUnichar *val;
    nsAString val_str;
    nsresult nsres;
    HRESULT hres = S_OK;

    nsres = get_elem_attr_value(elem->dom_element, name, &val_str, &val);
    if(NS_FAILED(nsres))
        return E_FAIL;

    TRACE("%s: returning %s\n", debugstr_w(name), debugstr_w(val));

    if(*val || !use_null) {
        *p = SysAllocString(val);
        if(!*p)
            hres = E_OUTOFMEMORY;
    }else {
        *p = NULL;
    }
    nsAString_Finish(&val_str);
    return hres;
}

HRESULT elem_string_attr_setter(HTMLElement *elem, const WCHAR *name, const WCHAR *value)
{
    nsAString name_str, val_str;
    nsresult nsres;

    nsAString_InitDepend(&name_str, name);
    nsAString_InitDepend(&val_str, value);
    nsres = nsIDOMElement_SetAttribute(elem->dom_element, &name_str, &val_str);
    nsAString_Finish(&name_str);
    nsAString_Finish(&val_str);

    if(NS_FAILED(nsres)) {
        WARN("SetAttribute failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static VARIANT_BOOL element_has_attribute(HTMLElement *element, const WCHAR *name)
{
    nsAString name_str;
    cpp_bool r;
    nsresult nsres;

    if(!element->dom_element) {
        WARN("no DOM element\n");
        return VARIANT_FALSE;
    }

    nsAString_InitDepend(&name_str, name);
    nsres = nsIDOMElement_HasAttribute(element->dom_element, &name_str, &r);
    return variant_bool(NS_SUCCEEDED(nsres) && r);
}

static HRESULT element_remove_attribute(HTMLElement *element, const WCHAR *name)
{
    nsAString name_str;
    nsresult nsres;

    if(!element->dom_element) {
        WARN("no DOM element\n");
        return S_OK;
    }

    nsAString_InitDepend(&name_str, name);
    nsres = nsIDOMElement_RemoveAttribute(element->dom_element, &name_str);
    nsAString_Finish(&name_str);
    return map_nsresult(nsres);
}

HRESULT get_readystate_string(READYSTATE readystate, BSTR *p)
{
    static const LPCWSTR readystate_strs[] = {
        L"uninitialized",
        L"loading",
        L"loaded",
        L"interactive",
        L"complete"
    };

    assert(readystate <= READYSTATE_COMPLETE);
    *p = SysAllocString(readystate_strs[readystate]);
    return *p ? S_OK : E_OUTOFMEMORY;
}

typedef struct
{
    DispatchEx dispex;
    IHTMLFiltersCollection IHTMLFiltersCollection_iface;

    LONG ref;
} HTMLFiltersCollection;

static inline HTMLFiltersCollection *impl_from_IHTMLFiltersCollection(IHTMLFiltersCollection *iface)
{
    return CONTAINING_RECORD(iface, HTMLFiltersCollection, IHTMLFiltersCollection_iface);
}

static HRESULT create_filters_collection(HTMLElement *elem, IHTMLFiltersCollection **ret);

static inline HTMLElement *impl_from_IHTMLElement(IHTMLElement *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IHTMLElement_iface);
}

static HRESULT copy_nselem_attrs(nsIDOMElement *nselem_with_attrs, nsIDOMElement *nselem)
{
    nsIDOMMozNamedAttrMap *attrs;
    nsAString name_str, val_str;
    nsresult nsres, nsres2;
    nsIDOMAttr *attr;
    UINT32 i, length;

    nsres = nsIDOMElement_GetAttributes(nselem_with_attrs, &attrs);
    if(NS_FAILED(nsres))
        return E_FAIL;

    nsres = nsIDOMMozNamedAttrMap_GetLength(attrs, &length);
    if(NS_FAILED(nsres)) {
        nsIDOMMozNamedAttrMap_Release(attrs);
        return E_FAIL;
    }

    nsAString_Init(&name_str, NULL);
    nsAString_Init(&val_str, NULL);
    for(i = 0; i < length; i++) {
        nsres = nsIDOMMozNamedAttrMap_Item(attrs, i, &attr);
        if(NS_FAILED(nsres))
            continue;

        nsres  = nsIDOMAttr_GetNodeName(attr, &name_str);
        nsres2 = nsIDOMAttr_GetNodeValue(attr, &val_str);
        nsIDOMAttr_Release(attr);
        if(NS_FAILED(nsres) || NS_FAILED(nsres2))
            continue;

        nsIDOMElement_SetAttribute(nselem, &name_str, &val_str);
    }
    nsAString_Finish(&name_str);
    nsAString_Finish(&val_str);

    nsIDOMMozNamedAttrMap_Release(attrs);
    return S_OK;
}

static HRESULT create_nselem_parse(HTMLDocumentNode *doc, const WCHAR *tag, nsIDOMElement **ret)
{
    static const WCHAR prefix[4] = L"<FOO";
    nsIDOMDocumentFragment *nsfragment;
    WCHAR *p = wcschr(tag + 1, '>');
    UINT32 i, name_len, size;
    nsIDOMElement *nselem;
    nsIDOMRange *nsrange;
    nsIDOMNode *nsnode;
    nsresult nsres;
    nsAString str;
    HRESULT hres;

    if(!p || p[1] || wcschr(tag + 1, '<'))
        return E_FAIL;
    if(!doc->dom_document) {
        WARN("NULL dom_document\n");
        return E_UNEXPECTED;
    }

    /* Ignore the starting token and > or /> end token */
    name_len = p - tag - 1 - (p[-1] == '/');

    /* Get the tag name using HTML whitespace rules */
    for(i = 1; i <= name_len; i++) {
        if((tag[i] >= 0x09 && tag[i] <= 0x0d) || tag[i] == ' ') {
            name_len = i - 1;
            break;
        }
    }
    if(!name_len)
        return E_FAIL;
    size = (p + 2 - (tag + 1 + name_len)) * sizeof(WCHAR);

    /* Parse the input via a contextual fragment, using a dummy unknown tag */
    nsres = nsIDOMDocument_CreateRange(doc->dom_document, &nsrange);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);

    if(!(p = malloc(sizeof(prefix) + size))) {
        nsIDOMRange_Release(nsrange);
        return E_OUTOFMEMORY;
    }
    memcpy(p, prefix, sizeof(prefix));
    memcpy(p + ARRAY_SIZE(prefix), tag + 1 + name_len, size);

    nsAString_InitDepend(&str, p);
    nsIDOMRange_CreateContextualFragment(nsrange, &str, &nsfragment);
    nsIDOMRange_Release(nsrange);
    nsAString_Finish(&str);
    free(p);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);

    /* Grab the parsed element and copy its attributes into the proper element */
    nsres = nsIDOMDocumentFragment_GetFirstChild(nsfragment, &nsnode);
    nsIDOMDocumentFragment_Release(nsfragment);
    if(NS_FAILED(nsres) || !nsnode)
        return E_FAIL;

    nsres = nsIDOMNode_QueryInterface(nsnode, &IID_nsIDOMElement, (void**)&nselem);
    nsIDOMNode_Release(nsnode);
    if(NS_FAILED(nsres))
        return E_FAIL;

    if(!(p = malloc((name_len + 1) * sizeof(WCHAR))))
        hres = E_OUTOFMEMORY;
    else {
        memcpy(p, tag + 1, name_len * sizeof(WCHAR));
        p[name_len] = '\0';

        nsAString_InitDepend(&str, p);
        nsres = nsIDOMDocument_CreateElement(doc->dom_document, &str, ret);
        nsAString_Finish(&str);
        free(p);

        if(NS_FAILED(nsres))
            hres = map_nsresult(nsres);
        else {
            hres = copy_nselem_attrs(nselem, *ret);
            if(FAILED(hres))
                nsIDOMElement_Release(*ret);
        }
    }
    nsIDOMElement_Release(nselem);
    return hres;
}

HRESULT create_nselem(HTMLDocumentNode *doc, const WCHAR *tag, nsIDOMElement **ret)
{
    nsAString tag_str;
    nsresult nsres;

    if(!doc->dom_document) {
        WARN("NULL dom_document\n");
        return E_UNEXPECTED;
    }

    nsAString_InitDepend(&tag_str, tag);
    nsres = nsIDOMDocument_CreateElement(doc->dom_document, &tag_str, ret);
    nsAString_Finish(&tag_str);
    if(NS_FAILED(nsres)) {
        ERR("CreateElement failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT create_element(HTMLDocumentNode *doc, const WCHAR *tag, HTMLElement **ret)
{
    nsIDOMElement *nselem;
    HRESULT hres;

    /* Use owner doc if called on document fragment */
    if(!doc->dom_document)
        doc = doc->node.doc;

    /* IE8 and below allow creating elements with attributes, such as <div class="a"> */
    if(tag[0] == '<' && dispex_compat_mode(&doc->node.event_target.dispex) <= COMPAT_MODE_IE8)
        hres = create_nselem_parse(doc, tag, &nselem);
    else
        hres = create_nselem(doc, tag, &nselem);
    if(FAILED(hres))
        return hres;

    hres = HTMLElement_Create(doc, (nsIDOMNode*)nselem, TRUE, ret);
    nsIDOMElement_Release(nselem);
    return hres;
}

static HTMLDOMAttribute *find_attr_in_list(HTMLAttributeCollection *attrs, DISPID dispid, nsIDOMAttr *nsattr, LONG *pos)
{
    HTMLDOMAttribute *iter, *ret = NULL;
    struct list *list = &attrs->attrs;
    unsigned i = 0;

    if(nsattr) {
        LIST_FOR_EACH_ENTRY(iter, list, HTMLDOMAttribute, entry) {
            if(iter->node.nsnode == (nsIDOMNode*)nsattr) {
                ret = iter;
                break;
            }
            i++;
        }
    }else {
        LIST_FOR_EACH_ENTRY(iter, list, HTMLDOMAttribute, entry) {
            if(iter->dispid == dispid) {
                ret = iter;
                break;
            }
            i++;
        }
    }
    if(pos)
        *pos = i;
    return ret;
}

static DISPID get_dispid_for_nsattr(HTMLElement *elem, nsIDOMAttr *nsattr)
{
    DISPID dispid = 0;
    nsAString nsstr;

    nsAString_InitDepend(&nsstr, NULL);
    if(SUCCEEDED(nsIDOMAttr_GetName(nsattr, &nsstr))) {
        const PRUnichar *name;
        nsAString_GetData(&nsstr, &name);

        /* Supply a dummy non-builtin dispid so it can be checked for expando */
        if(is_custom_attribute(&elem->node.event_target.dispex, name))
            dispid = MSHTML_DISPID_CUSTOM_MIN;
        nsAString_Finish(&nsstr);
    }
    return dispid;
}

typedef struct {
    DispatchEx dispex;
    IHTMLRect IHTMLRect_iface;
    IHTMLRect2 IHTMLRect2_iface;

    LONG ref;

    nsIDOMClientRect *nsrect;
} HTMLRect;

static inline HTMLRect *impl_from_IHTMLRect(IHTMLRect *iface)
{
    return CONTAINING_RECORD(iface, HTMLRect, IHTMLRect_iface);
}

static HRESULT WINAPI HTMLRect_QueryInterface(IHTMLRect *iface, REFIID riid, void **ppv)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        *ppv = &This->IHTMLRect_iface;
    }else if(IsEqualGUID(&IID_IHTMLRect, riid)) {
        *ppv = &This->IHTMLRect_iface;
    }else if (IsEqualGUID(&IID_IHTMLRect2, riid)) {
        *ppv = &This->IHTMLRect2_iface;
    }else if(dispex_query_interface(&This->dispex, riid, ppv)) {
        return *ppv ? S_OK : E_NOINTERFACE;
    }else {
        FIXME("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI HTMLRect_AddRef(IHTMLRect *iface)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI HTMLRect_Release(IHTMLRect *iface)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
        if(This->nsrect)
            nsIDOMClientRect_Release(This->nsrect);
        release_dispex(&This->dispex);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI HTMLRect_GetTypeInfoCount(IHTMLRect *iface, UINT *pctinfo)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    FIXME("(%p)->(%p)\n", This, pctinfo);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLRect_GetTypeInfo(IHTMLRect *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);

    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLRect_GetIDsOfNames(IHTMLRect *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);

    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLRect_Invoke(IHTMLRect *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
        VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);

    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid, wFlags,
            pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLRect_put_left(IHTMLRect *iface, LONG v)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLRect_get_left(IHTMLRect *iface, LONG *p)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    float left;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMClientRect_GetLeft(This->nsrect, &left);
    if(NS_FAILED(nsres)) {
        ERR("GetLeft failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = floor(left+0.5);
    return S_OK;
}

static HRESULT WINAPI HTMLRect_put_top(IHTMLRect *iface, LONG v)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLRect_get_top(IHTMLRect *iface, LONG *p)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    float top;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMClientRect_GetTop(This->nsrect, &top);
    if(NS_FAILED(nsres)) {
        ERR("GetTop failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = floor(top+0.5);
    return S_OK;
}

static HRESULT WINAPI HTMLRect_put_right(IHTMLRect *iface, LONG v)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLRect_get_right(IHTMLRect *iface, LONG *p)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    float right;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMClientRect_GetRight(This->nsrect, &right);
    if(NS_FAILED(nsres)) {
        ERR("GetRight failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = floor(right+0.5);
    return S_OK;
}

static HRESULT WINAPI HTMLRect_put_bottom(IHTMLRect *iface, LONG v)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    FIXME("(%p)->(%ld)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLRect_get_bottom(IHTMLRect *iface, LONG *p)
{
    HTMLRect *This = impl_from_IHTMLRect(iface);
    float bottom;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMClientRect_GetBottom(This->nsrect, &bottom);
    if(NS_FAILED(nsres)) {
        ERR("GetBottom failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = floor(bottom+0.5);
    return S_OK;
}

static inline HTMLRect *impl_from_IHTMLRect2(IHTMLRect2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLRect, IHTMLRect2_iface);
}

static HRESULT WINAPI HTMLRect2_QueryInterface(IHTMLRect2 *iface, REFIID riid, void **ppv)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    return IHTMLRect_QueryInterface(&This->IHTMLRect_iface, riid, ppv);
}

static ULONG WINAPI HTMLRect2_AddRef(IHTMLRect2 *iface)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    return IHTMLRect_AddRef(&This->IHTMLRect_iface);
}

static ULONG WINAPI HTMLRect2_Release(IHTMLRect2 *iface)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    return IHTMLRect_Release(&This->IHTMLRect_iface);
}

static HRESULT WINAPI HTMLRect2_GetTypeInfoCount(IHTMLRect2 *iface, UINT *pctinfo)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    return IDispatchEx_GetTypeInfoCount(&This->dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLRect2_GetTypeInfo(IHTMLRect2 *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLRect2_GetIDsOfNames(IHTMLRect2 *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLRect2_Invoke(IHTMLRect2 *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
        VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid, wFlags,
            pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLRect2_get_width(IHTMLRect2 *iface, FLOAT *p)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMClientRect_GetWidth(This->nsrect, p);
    if (NS_FAILED(nsres)) {
        ERR("GetWidth failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLRect2_get_height(IHTMLRect2 *iface, FLOAT *p)
{
    HTMLRect *This = impl_from_IHTMLRect2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMClientRect_GetHeight(This->nsrect, p);
    if (NS_FAILED(nsres)) {
        ERR("GetHeight failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static const IHTMLRectVtbl HTMLRectVtbl = {
    HTMLRect_QueryInterface,
    HTMLRect_AddRef,
    HTMLRect_Release,
    HTMLRect_GetTypeInfoCount,
    HTMLRect_GetTypeInfo,
    HTMLRect_GetIDsOfNames,
    HTMLRect_Invoke,
    HTMLRect_put_left,
    HTMLRect_get_left,
    HTMLRect_put_top,
    HTMLRect_get_top,
    HTMLRect_put_right,
    HTMLRect_get_right,
    HTMLRect_put_bottom,
    HTMLRect_get_bottom
};

static const IHTMLRect2Vtbl HTMLRect2Vtbl = {
    HTMLRect2_QueryInterface,
    HTMLRect2_AddRef,
    HTMLRect2_Release,
    HTMLRect2_GetTypeInfoCount,
    HTMLRect2_GetTypeInfo,
    HTMLRect2_GetIDsOfNames,
    HTMLRect2_Invoke,
    HTMLRect2_get_width,
    HTMLRect2_get_height,
};

void HTMLRect_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    if (mode >= COMPAT_MODE_IE9)
        dispex_info_add_interface(info, IHTMLRect2_tid, NULL);
}

static const tid_t HTMLRect_iface_tids[] = {
    IHTMLRect_tid,
    0
};
dispex_static_data_t HTMLRect_dispex = {
    L"ClientRect",
    NULL,
    PROTO_ID_HTMLRect,
    IHTMLRect_tid,
    HTMLRect_iface_tids,
    HTMLRect_init_dispex_info
};

static HRESULT create_html_rect(nsIDOMClientRect *nsrect, HTMLInnerWindow *window, compat_mode_t compat_mode,
        IHTMLRect **ret)
{
    HTMLRect *rect;

    rect = calloc(1, sizeof(HTMLRect));
    if(!rect)
        return E_OUTOFMEMORY;

    rect->IHTMLRect_iface.lpVtbl = &HTMLRectVtbl;
    rect->IHTMLRect2_iface.lpVtbl = &HTMLRect2Vtbl;
    rect->ref = 1;

    init_dispatch(&rect->dispex, (IUnknown*)&rect->IHTMLRect_iface, &HTMLRect_dispex, window, compat_mode);

    nsIDOMClientRect_AddRef(nsrect);
    rect->nsrect = nsrect;

    *ret = &rect->IHTMLRect_iface;
    return S_OK;
}

typedef struct {
    DispatchEx dispex;
    IHTMLRectCollection IHTMLRectCollection_iface;

    LONG ref;

    HTMLInnerWindow *window;
    nsIDOMClientRectList *rect_list;
} HTMLRectCollection;

typedef struct {
    IEnumVARIANT IEnumVARIANT_iface;

    LONG ref;

    ULONG iter;
    HTMLRectCollection *col;
} HTMLRectCollectionEnum;

static inline HTMLRectCollectionEnum *HTMLRectCollectionEnum_from_IEnumVARIANT(IEnumVARIANT *iface)
{
    return CONTAINING_RECORD(iface, HTMLRectCollectionEnum, IEnumVARIANT_iface);
}

static HRESULT WINAPI HTMLRectCollectionEnum_QueryInterface(IEnumVARIANT *iface, REFIID riid, void **ppv)
{
    HTMLRectCollectionEnum *This = HTMLRectCollectionEnum_from_IEnumVARIANT(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);

    if(IsEqualGUID(riid, &IID_IUnknown)) {
        *ppv = &This->IEnumVARIANT_iface;
    }else if(IsEqualGUID(riid, &IID_IEnumVARIANT)) {
        *ppv = &This->IEnumVARIANT_iface;
    }else {
        FIXME("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI HTMLRectCollectionEnum_AddRef(IEnumVARIANT *iface)
{
    HTMLRectCollectionEnum *This = HTMLRectCollectionEnum_from_IEnumVARIANT(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI HTMLRectCollectionEnum_Release(IEnumVARIANT *iface)
{
    HTMLRectCollectionEnum *This = HTMLRectCollectionEnum_from_IEnumVARIANT(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
        IHTMLRectCollection_Release(&This->col->IHTMLRectCollection_iface);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI HTMLRectCollectionEnum_Next(IEnumVARIANT *iface, ULONG celt, VARIANT *rgVar, ULONG *pCeltFetched)
{
    HTMLRectCollectionEnum *This = HTMLRectCollectionEnum_from_IEnumVARIANT(iface);
    VARIANT index;
    HRESULT hres;
    ULONG num, i;
    UINT32 len;

    TRACE("(%p)->(%lu %p %p)\n", This, celt, rgVar, pCeltFetched);

    nsIDOMClientRectList_GetLength(This->col->rect_list, &len);
    num = min(len - This->iter, celt);
    V_VT(&index) = VT_I4;

    for(i = 0; i < num; i++) {
        V_I4(&index) = This->iter + i;
        hres = IHTMLRectCollection_item(&This->col->IHTMLRectCollection_iface, &index, &rgVar[i]);
        if(FAILED(hres)) {
            while(i--)
                VariantClear(&rgVar[i]);
            return hres;
        }
    }

    This->iter += num;
    if(pCeltFetched)
        *pCeltFetched = num;
    return num == celt ? S_OK : S_FALSE;
}

static HRESULT WINAPI HTMLRectCollectionEnum_Skip(IEnumVARIANT *iface, ULONG celt)
{
    HTMLRectCollectionEnum *This = HTMLRectCollectionEnum_from_IEnumVARIANT(iface);
    UINT32 len;

    TRACE("(%p)->(%lu)\n", This, celt);

    nsIDOMClientRectList_GetLength(This->col->rect_list, &len);
    if(This->iter + celt > len) {
        This->iter = len;
        return S_FALSE;
    }

    This->iter += celt;
    return S_OK;
}

static HRESULT WINAPI HTMLRectCollectionEnum_Reset(IEnumVARIANT *iface)
{
    HTMLRectCollectionEnum *This = HTMLRectCollectionEnum_from_IEnumVARIANT(iface);

    TRACE("(%p)->()\n", This);

    This->iter = 0;
    return S_OK;
}

static HRESULT WINAPI HTMLRectCollectionEnum_Clone(IEnumVARIANT *iface, IEnumVARIANT **ppEnum)
{
    HTMLRectCollectionEnum *This = HTMLRectCollectionEnum_from_IEnumVARIANT(iface);
    FIXME("(%p)->(%p)\n", This, ppEnum);
    return E_NOTIMPL;
}

static const IEnumVARIANTVtbl HTMLRectCollectionEnumVtbl = {
    HTMLRectCollectionEnum_QueryInterface,
    HTMLRectCollectionEnum_AddRef,
    HTMLRectCollectionEnum_Release,
    HTMLRectCollectionEnum_Next,
    HTMLRectCollectionEnum_Skip,
    HTMLRectCollectionEnum_Reset,
    HTMLRectCollectionEnum_Clone
};

static inline HTMLRectCollection *impl_from_IHTMLRectCollection(IHTMLRectCollection *iface)
{
    return CONTAINING_RECORD(iface, HTMLRectCollection, IHTMLRectCollection_iface);
}

static HRESULT WINAPI HTMLRectCollection_QueryInterface(IHTMLRectCollection *iface, REFIID riid, void **ppv)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        *ppv = &This->IHTMLRectCollection_iface;
    }else if(IsEqualGUID(&IID_IHTMLRectCollection, riid)) {
        *ppv = &This->IHTMLRectCollection_iface;
    }else if(dispex_query_interface(&This->dispex, riid, ppv)) {
        return *ppv ? S_OK : E_NOINTERFACE;
    }else {
        FIXME("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI HTMLRectCollection_AddRef(IHTMLRectCollection *iface)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI HTMLRectCollection_Release(IHTMLRectCollection *iface)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
        if(This->rect_list)
            nsIDOMClientRectList_Release(This->rect_list);
        IHTMLWindow2_Release(&This->window->base.IHTMLWindow2_iface);
        release_dispex(&This->dispex);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI HTMLRectCollection_GetTypeInfoCount(IHTMLRectCollection *iface, UINT *pctinfo)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    FIXME("(%p)->(%p)\n", This, pctinfo);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLRectCollection_GetTypeInfo(IHTMLRectCollection *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLRectCollection_GetIDsOfNames(IHTMLRectCollection *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLRectCollection_Invoke(IHTMLRectCollection *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
        VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid, wFlags,
            pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLRectCollection_get_length(IHTMLRectCollection *iface, LONG *p)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    UINT32 length;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMClientRectList_GetLength(This->rect_list, &length);
    assert(nsres == NS_OK);
    *p = length;
    return S_OK;
}

static HRESULT WINAPI HTMLRectCollection_get__newEnum(IHTMLRectCollection *iface, IUnknown **p)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    HTMLRectCollectionEnum *ret;

    TRACE("(%p)->(%p)\n", This, p);

    ret = malloc(sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IEnumVARIANT_iface.lpVtbl = &HTMLRectCollectionEnumVtbl;
    ret->ref = 1;
    ret->iter = 0;

    HTMLRectCollection_AddRef(&This->IHTMLRectCollection_iface);
    ret->col = This;

    *p = (IUnknown*)&ret->IEnumVARIANT_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLRectCollection_item(IHTMLRectCollection *iface, VARIANT *index, VARIANT *result)
{
    HTMLRectCollection *This = impl_from_IHTMLRectCollection(iface);
    nsIDOMClientRect *nsrect;
    IHTMLRect *rect;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_variant(index), result);

    if(V_VT(index) != VT_I4 || V_I4(index) < 0) {
        FIXME("Unsupported for %s index\n", debugstr_variant(index));
        return E_NOTIMPL;
    }

    nsres = nsIDOMClientRectList_Item(This->rect_list, V_I4(index), &nsrect);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);
    if(!nsrect) {
        V_VT(result) = VT_NULL;
        return S_OK;
    }

    hres = create_html_rect(nsrect, This->window, dispex_compat_mode(&This->dispex), &rect);
    nsIDOMClientRect_Release(nsrect);
    if(FAILED(hres))
        return hres;

    V_VT(result) = VT_DISPATCH;
    V_DISPATCH(result) = (IDispatch *)rect;
    return S_OK;
}

static const IHTMLRectCollectionVtbl HTMLRectCollectionVtbl = {
    HTMLRectCollection_QueryInterface,
    HTMLRectCollection_AddRef,
    HTMLRectCollection_Release,
    HTMLRectCollection_GetTypeInfoCount,
    HTMLRectCollection_GetTypeInfo,
    HTMLRectCollection_GetIDsOfNames,
    HTMLRectCollection_Invoke,
    HTMLRectCollection_get_length,
    HTMLRectCollection_get__newEnum,
    HTMLRectCollection_item
};

static inline HTMLRectCollection *HTMLRectCollection_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLRectCollection, dispex);
}

static HRESULT HTMLRectCollection_get_dispid(DispatchEx *dispex, BSTR name, DWORD flags, DISPID *dispid)
{
    HTMLRectCollection *This = HTMLRectCollection_from_DispatchEx(dispex);
    UINT32 len = 0;
    DWORD idx = 0;
    WCHAR *ptr;

    for(ptr = name; *ptr && is_digit(*ptr); ptr++)
        idx = idx*10 + (*ptr-'0');
    if(*ptr)
        return DISP_E_UNKNOWNNAME;

    nsIDOMClientRectList_GetLength(This->rect_list, &len);
    if(idx >= len)
        return DISP_E_UNKNOWNNAME;

    *dispid = MSHTML_DISPID_CUSTOM_MIN + idx;
    TRACE("ret %lx\n", *dispid);
    return S_OK;
}

static HRESULT HTMLRectCollection_get_name(DispatchEx *dispex, DISPID id, BSTR *name)
{
    HTMLRectCollection *This = HTMLRectCollection_from_DispatchEx(dispex);
    DWORD idx = id - MSHTML_DISPID_CUSTOM_MIN;
    UINT32 len = 0;
    WCHAR buf[11];

    nsIDOMClientRectList_GetLength(This->rect_list, &len);
    if(idx >= len)
        return DISP_E_MEMBERNOTFOUND;

    len = swprintf(buf, ARRAY_SIZE(buf), L"%u", idx);
    return (*name = SysAllocStringLen(buf, len)) ? S_OK : E_OUTOFMEMORY;
}

static HRESULT HTMLRectCollection_invoke(DispatchEx *dispex, IDispatch *this_obj, DISPID id, LCID lcid, WORD flags,
        DISPPARAMS *params, VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    HTMLRectCollection *This = HTMLRectCollection_from_DispatchEx(dispex);

    TRACE("(%p)->(%lx %lx %x %p %p %p %p)\n", This, id, lcid, flags, params, res, ei, caller);

    switch(flags) {
    case DISPATCH_PROPERTYGET: {
        nsIDOMClientRect *rect;
        IHTMLRect *html_rect;
        nsresult nsres;
        HRESULT hres;

        nsres = nsIDOMClientRectList_Item(This->rect_list, id - MSHTML_DISPID_CUSTOM_MIN, &rect);
        if(NS_FAILED(nsres) || !rect) {
            WARN("Unknown item\n");
            return DISP_E_MEMBERNOTFOUND;
        }

        hres = create_html_rect(rect, This->window, dispex_compat_mode(&This->dispex), &html_rect);
        nsIDOMClientRect_Release(rect);
        if(FAILED(hres))
            return hres;

        V_VT(res) = VT_DISPATCH;
        V_DISPATCH(res) = (IDispatch*)html_rect;
        break;
    }

    default:
        FIXME("unimplemented flags %x\n", flags);
        return E_NOTIMPL;
    }

    return S_OK;
}

static const dispex_static_data_vtbl_t HTMLRectCollection_dispex_vtbl = {
    NULL,
    HTMLRectCollection_get_dispid,
    HTMLRectCollection_get_name,
    HTMLRectCollection_invoke,
    NULL
};
static const tid_t HTMLRectCollection_iface_tids[] = {
    IHTMLRectCollection_tid,
    0
};
dispex_static_data_t HTMLRectCollection_dispex = {
    L"ClientRectList",
    &HTMLRectCollection_dispex_vtbl,
    PROTO_ID_HTMLRectCollection,
    IHTMLRectCollection_tid,
    HTMLRectCollection_iface_tids
};

static HRESULT WINAPI HTMLElement_QueryInterface(IHTMLElement *iface,
                                                 REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    return IHTMLDOMNode_QueryInterface(&This->node.IHTMLDOMNode_iface, riid, ppv);
}

static ULONG WINAPI HTMLElement_AddRef(IHTMLElement *iface)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    return IHTMLDOMNode_AddRef(&This->node.IHTMLDOMNode_iface);
}

static ULONG WINAPI HTMLElement_Release(IHTMLElement *iface)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    return IHTMLDOMNode_Release(&This->node.IHTMLDOMNode_iface);
}

static HRESULT WINAPI HTMLElement_GetTypeInfoCount(IHTMLElement *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLElement_GetTypeInfo(IHTMLElement *iface, UINT iTInfo,
                                              LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLElement_GetIDsOfNames(IHTMLElement *iface, REFIID riid,
                                                LPOLESTR *rgszNames, UINT cNames,
                                                LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLElement_Invoke(IHTMLElement *iface, DISPID dispIdMember,
                            REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                            VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static inline WCHAR *translate_attr_name(WCHAR *attr_name, compat_mode_t compat_mode)
{
    static WCHAR classNameW[] = L"className";
    WCHAR *ret = attr_name;

    if(compat_mode >= COMPAT_MODE_IE8 && !wcsicmp(attr_name, L"class"))
        ret = classNameW;
    return ret;
}

static HRESULT set_elem_attr_value_by_dispid(HTMLElement *elem, DISPID dispid, VARIANT *v)
{
    DISPID propput_dispid = DISPID_PROPERTYPUT;
    DISPPARAMS dp = {v, &propput_dispid, 1, 1};
    EXCEPINFO ei;

    if(dispid == DISPID_IHTMLELEMENT_STYLE) {
        TRACE("Ignoring call on style attribute\n");
        return S_OK;
    }

    return IDispatchEx_InvokeEx(&elem->node.event_target.dispex.IDispatchEx_iface, dispid,
            LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYPUT, &dp, NULL, &ei, NULL);
}

static HRESULT WINAPI HTMLElement_setAttribute(IHTMLElement *iface, BSTR strAttributeName,
                                               VARIANT AttributeValue, LONG lFlags)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    compat_mode_t compat_mode = dispex_compat_mode(&This->node.event_target.dispex);
    nsAString name_str, value_str;
    VARIANT val = AttributeValue;
    BOOL needs_free = FALSE;
    nsresult nsres;
    DISPID dispid;
    HRESULT hres;

    TRACE("(%p)->(%s %s %08lx)\n", This, debugstr_w(strAttributeName), debugstr_variant(&AttributeValue), lFlags);

    if(compat_mode < COMPAT_MODE_IE9 || !This->dom_element) {
        hres = IDispatchEx_GetDispID(&This->node.event_target.dispex.IDispatchEx_iface, translate_attr_name(strAttributeName, compat_mode),
                (lFlags&ATTRFLAG_CASESENSITIVE ? fdexNameCaseSensitive : fdexNameCaseInsensitive) | fdexNameEnsure, &dispid);
        if(FAILED(hres))
            return hres;

        if(compat_mode >= COMPAT_MODE_IE8 && get_dispid_type(dispid) == DISPEXPROP_BUILTIN) {
            if(V_VT(&val) != VT_BSTR && V_VT(&val) != VT_NULL) {
                LCID lcid = MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),SORT_DEFAULT);

                V_VT(&val) = VT_EMPTY;
                hres = VariantChangeTypeEx(&val, &AttributeValue, lcid, 0, VT_BSTR);
                if(FAILED(hres))
                    return hres;

                if(V_BSTR(&val))
                    needs_free = TRUE;
                else
                    V_VT(&val) = VT_NULL;
            }
        }

        /* className and style are special cases */
        if(compat_mode != COMPAT_MODE_IE8 || !This->dom_element ||
           (dispid != DISPID_IHTMLELEMENT_CLASSNAME && dispid != DISPID_IHTMLELEMENT_STYLE)) {
            hres = set_elem_attr_value_by_dispid(This, dispid, &val);
            goto done;
        }
    }

    hres = variant_to_nsstr(&val, FALSE, &value_str);
    if(FAILED(hres))
        goto done;

    nsAString_InitDepend(&name_str, strAttributeName);
    nsres = nsIDOMElement_SetAttribute(This->dom_element, &name_str, &value_str);
    nsAString_Finish(&name_str);
    nsAString_Finish(&value_str);
    if(NS_FAILED(nsres))
        WARN("SetAttribute failed: %08lx\n", nsres);
    hres = map_nsresult(nsres);

done:
    if(needs_free)
        SysFreeString(V_BSTR(&val));
    return hres;
}

HRESULT get_elem_attr_value_by_dispid(HTMLElement *elem, DISPID dispid, VARIANT *ret)
{
    DISPPARAMS dispParams = {NULL, NULL, 0, 0};
    EXCEPINFO excep;

    return IDispatchEx_InvokeEx(&elem->node.event_target.dispex.IDispatchEx_iface, dispid, LOCALE_SYSTEM_DEFAULT,
            DISPATCH_PROPERTYGET, &dispParams, ret, &excep, NULL);
}

HRESULT attr_value_to_string(VARIANT *v)
{
    HRESULT hres;

    switch(V_VT(v)) {
    case VT_BSTR:
        break;
    case VT_NULL:
        V_BSTR(v) = SysAllocString(L"null");
        if(!V_BSTR(v))
            return E_OUTOFMEMORY;
        V_VT(v) = VT_BSTR;
        break;
    case VT_DISPATCH:
        IDispatch_Release(V_DISPATCH(v));
        V_VT(v) = VT_BSTR;
        V_BSTR(v) = SysAllocString(NULL);
        break;
    default:
        hres = VariantChangeType(v, v, 0, VT_BSTR);
        if(FAILED(hres))
            return hres;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_getAttribute(IHTMLElement *iface, BSTR strAttributeName,
                                               LONG lFlags, VARIANT *AttributeValue)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    compat_mode_t compat_mode = dispex_compat_mode(&This->node.event_target.dispex);
    nsAString name_str, value_str;
    nsresult nsres;
    DISPID dispid;
    HRESULT hres;

    TRACE("(%p)->(%s %08lx %p)\n", This, debugstr_w(strAttributeName), lFlags, AttributeValue);

    if(lFlags & ~(ATTRFLAG_CASESENSITIVE|ATTRFLAG_ASSTRING))
        FIXME("Unsupported flags %lx\n", lFlags);

    if(compat_mode < COMPAT_MODE_IE9 || !This->dom_element) {
        hres = IDispatchEx_GetDispID(&This->node.event_target.dispex.IDispatchEx_iface, translate_attr_name(strAttributeName, compat_mode),
                                     lFlags&ATTRFLAG_CASESENSITIVE ? fdexNameCaseSensitive : fdexNameCaseInsensitive, &dispid);
        if(FAILED(hres)) {
            V_VT(AttributeValue) = VT_NULL;
            return (hres == DISP_E_UNKNOWNNAME) ? S_OK : hres;
        }

        /* className and style are special cases */
        if(compat_mode != COMPAT_MODE_IE8 || !This->dom_element ||
           (dispid != DISPID_IHTMLELEMENT_CLASSNAME && dispid != DISPID_IHTMLELEMENT_STYLE)) {
            hres = get_elem_attr_value_by_dispid(This, dispid, AttributeValue);
            if(FAILED(hres))
                return hres;

            if(compat_mode >= COMPAT_MODE_IE8 && V_VT(AttributeValue) != VT_BSTR && V_VT(AttributeValue) != VT_NULL) {
                LCID lcid = MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),SORT_DEFAULT);

                hres = VariantChangeTypeEx(AttributeValue, AttributeValue, lcid, 0, VT_BSTR);
                if(FAILED(hres)) {
                    VariantClear(AttributeValue);
                    return hres;
                }
                if(!V_BSTR(AttributeValue))
                    V_VT(AttributeValue) = VT_NULL;
            }else if(lFlags & ATTRFLAG_ASSTRING)
                hres = attr_value_to_string(AttributeValue);
            return hres;
        }
    }

    nsAString_InitDepend(&name_str, strAttributeName);
    nsAString_InitDepend(&value_str, NULL);
    nsres = nsIDOMElement_GetAttribute(This->dom_element, &name_str, &value_str);
    nsAString_Finish(&name_str);
    return return_nsstr_variant(nsres, &value_str, 0, AttributeValue);
}

static HRESULT WINAPI HTMLElement_removeAttribute(IHTMLElement *iface, BSTR strAttributeName,
                                                  LONG lFlags, VARIANT_BOOL *pfSuccess)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    compat_mode_t compat_mode = dispex_compat_mode(&This->node.event_target.dispex);
    DISPID id;
    HRESULT hres;

    TRACE("(%p)->(%s %lx %p)\n", This, debugstr_w(strAttributeName), lFlags, pfSuccess);

    if(compat_mode < COMPAT_MODE_IE9 || !This->dom_element) {
        hres = IDispatchEx_GetDispID(&This->node.event_target.dispex.IDispatchEx_iface, translate_attr_name(strAttributeName, compat_mode),
                                     lFlags&ATTRFLAG_CASESENSITIVE ? fdexNameCaseSensitive : fdexNameCaseInsensitive, &id);
        if(hres == DISP_E_UNKNOWNNAME) {
            *pfSuccess = VARIANT_FALSE;
            return S_OK;
        }
        if(FAILED(hres))
            return hres;

        if(id == DISPID_IHTMLELEMENT_STYLE) {
            IHTMLStyle *style;

            TRACE("Special case: style\n");

            hres = IHTMLElement_get_style(&This->IHTMLElement_iface, &style);
            if(FAILED(hres))
                return hres;

            hres = IHTMLStyle_put_cssText(style, NULL);
            IHTMLStyle_Release(style);
            if(FAILED(hres))
                return hres;

            if(compat_mode >= COMPAT_MODE_IE8)
                element_remove_attribute(This, strAttributeName);

            *pfSuccess = VARIANT_TRUE;
            return S_OK;
        }

        if(compat_mode != COMPAT_MODE_IE8 || !This->dom_element || id != DISPID_IHTMLELEMENT_CLASSNAME)
            return remove_attribute(&This->node.event_target.dispex, id, pfSuccess);
    }

    *pfSuccess = element_has_attribute(This, strAttributeName);
    if(*pfSuccess)
        return element_remove_attribute(This, strAttributeName);
    return S_OK;
}

static HRESULT WINAPI HTMLElement_put_className(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString classname_str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&classname_str, v);
    nsres = nsIDOMElement_SetClassName(This->dom_element, &classname_str);
    nsAString_Finish(&classname_str);
    if(NS_FAILED(nsres))
        ERR("SetClassName failed: %08lx\n", nsres);

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_className(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString class_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsAString_Init(&class_str, NULL);
    nsres = nsIDOMElement_GetClassName(This->dom_element, &class_str);
    return return_nsstr(nsres, &class_str, p);
}

static HRESULT WINAPI HTMLElement_put_id(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString id_str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->dom_element) {
        FIXME("comment element\n");
        return S_OK;
    }

    nsAString_InitDepend(&id_str, v);
    nsres = nsIDOMElement_SetId(This->dom_element, &id_str);
    nsAString_Finish(&id_str);
    if(NS_FAILED(nsres))
        ERR("SetId failed: %08lx\n", nsres);

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_id(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString id_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        *p = NULL;
        return S_OK;
    }

    nsAString_Init(&id_str, NULL);
    nsres = nsIDOMElement_GetId(This->dom_element, &id_str);
    return return_nsstr(nsres, &id_str, p);
}

static HRESULT WINAPI HTMLElement_get_tagName(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString tag_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        TRACE("comment element\n");
        *p = SysAllocString(L"!");
        return *p ? S_OK : E_OUTOFMEMORY;
    }

    nsAString_Init(&tag_str, NULL);
    nsres = nsIDOMElement_GetTagName(This->dom_element, &tag_str);
    return return_nsstr(nsres, &tag_str, p);
}

static HRESULT WINAPI HTMLElement_get_parentElement(IHTMLElement *iface, IHTMLElement **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    IHTMLDOMNode *node;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    hres = IHTMLDOMNode_get_parentNode(&This->node.IHTMLDOMNode_iface, &node);
    if(FAILED(hres))
        return hres;

    if(!node) {
        *p = NULL;
        return S_OK;
    }

    hres = IHTMLDOMNode_QueryInterface(node, &IID_IHTMLElement, (void**)p);
    IHTMLDOMNode_Release(node);
    if(FAILED(hres))
        *p = NULL;

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_style(IHTMLElement *iface, IHTMLStyle **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->style) {
        HRESULT hres;

        hres = HTMLStyle_Create(This, &This->style);
        if(FAILED(hres))
            return hres;
    }

    *p = &This->style->IHTMLStyle_iface;
    IHTMLStyle_AddRef(*p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement_put_onhelp(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_HELP, &v);
}

static HRESULT WINAPI HTMLElement_get_onhelp(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_HELP, p);
}

static HRESULT WINAPI HTMLElement_put_onclick(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_CLICK, &v);
}

static HRESULT WINAPI HTMLElement_get_onclick(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_CLICK, p);
}

static HRESULT WINAPI HTMLElement_put_ondblclick(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_DBLCLICK, &v);
}

static HRESULT WINAPI HTMLElement_get_ondblclick(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_DBLCLICK, p);
}

static HRESULT WINAPI HTMLElement_put_onkeydown(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_KEYDOWN, &v);
}

static HRESULT WINAPI HTMLElement_get_onkeydown(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_KEYDOWN, p);
}

static HRESULT WINAPI HTMLElement_put_onkeyup(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_KEYUP, &v);
}

static HRESULT WINAPI HTMLElement_get_onkeyup(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_KEYUP, p);
}

static HRESULT WINAPI HTMLElement_put_onkeypress(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_KEYPRESS, &v);
}

static HRESULT WINAPI HTMLElement_get_onkeypress(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_KEYPRESS, p);
}

static HRESULT WINAPI HTMLElement_put_onmouseout(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_MOUSEOUT, &v);
}

static HRESULT WINAPI HTMLElement_get_onmouseout(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_MOUSEOUT, p);
}

static HRESULT WINAPI HTMLElement_put_onmouseover(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_MOUSEOVER, &v);
}

static HRESULT WINAPI HTMLElement_get_onmouseover(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_MOUSEOVER, p);
}

static HRESULT WINAPI HTMLElement_put_onmousemove(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_MOUSEMOVE, &v);
}

static HRESULT WINAPI HTMLElement_get_onmousemove(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_MOUSEMOVE, p);
}

static HRESULT WINAPI HTMLElement_put_onmousedown(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_MOUSEDOWN, &v);
}

static HRESULT WINAPI HTMLElement_get_onmousedown(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_MOUSEDOWN, p);
}

static HRESULT WINAPI HTMLElement_put_onmouseup(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_MOUSEUP, &v);
}

static HRESULT WINAPI HTMLElement_get_onmouseup(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_MOUSEUP, p);
}

static HRESULT WINAPI HTMLElement_get_document(IHTMLElement *iface, IDispatch **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    if(!p)
        return E_POINTER;

    if(This->node.vtbl->get_document)
        return This->node.vtbl->get_document(&This->node, p);

    *p = (IDispatch*)&This->node.doc->IHTMLDocument2_iface;
    IDispatch_AddRef(*p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement_put_title(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString title_str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->dom_element) {
        VARIANT *var;
        HRESULT hres;

        hres = dispex_get_dprop_ref(&This->node.event_target.dispex, L"title", TRUE, &var);
        if(FAILED(hres))
            return hres;

        VariantClear(var);
        V_VT(var) = VT_BSTR;
        V_BSTR(var) = v ? SysAllocString(v) : NULL;
        return S_OK;
    }

    if(!This->html_element)
        return elem_string_attr_setter(This, L"title", v);

    nsAString_InitDepend(&title_str, v);
    nsres = nsIDOMHTMLElement_SetTitle(This->html_element, &title_str);
    nsAString_Finish(&title_str);
    if(NS_FAILED(nsres))
        ERR("SetTitle failed: %08lx\n", nsres);

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_title(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString title_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        VARIANT *var;
        HRESULT hres;

        hres = dispex_get_dprop_ref(&This->node.event_target.dispex, L"title", FALSE, &var);
        if(hres == DISP_E_UNKNOWNNAME) {
            *p = NULL;
        }else if(V_VT(var) != VT_BSTR) {
            FIXME("title = %s\n", debugstr_variant(var));
            return E_FAIL;
        }else {
            *p = V_BSTR(var) ? SysAllocString(V_BSTR(var)) : NULL;
        }

        return S_OK;
    }

    if(!This->html_element)
        return elem_string_attr_getter(This, L"title", FALSE, p);

    nsAString_Init(&title_str, NULL);
    nsres = nsIDOMHTMLElement_GetTitle(This->html_element, &title_str);
    return return_nsstr(nsres, &title_str, p);
}

static HRESULT WINAPI HTMLElement_put_language(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    return elem_string_attr_setter(This, L"language", v);
}

static HRESULT WINAPI HTMLElement_get_language(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return elem_string_attr_getter(This, L"language", TRUE, p);
}

static HRESULT WINAPI HTMLElement_put_onselectstart(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_SELECTSTART, &v);
}

static HRESULT WINAPI HTMLElement_get_onselectstart(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_SELECTSTART, p);
}

static HRESULT WINAPI HTMLElement_scrollIntoView(IHTMLElement *iface, VARIANT varargStart)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    cpp_bool start = TRUE;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&varargStart));

    switch(V_VT(&varargStart)) {
    case VT_EMPTY:
    case VT_ERROR:
	break;
    case VT_BOOL:
	start = V_BOOL(&varargStart) != VARIANT_FALSE;
	break;
    default:
	FIXME("Unsupported argument %s\n", debugstr_variant(&varargStart));
    }

    if(!This->html_element) {
	FIXME("non-HTML elements\n");
	return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_ScrollIntoView(This->html_element, start, 1);
    assert(nsres == NS_OK);

    return S_OK;
}

static HRESULT WINAPI HTMLElement_contains(IHTMLElement *iface, IHTMLElement *pChild,
                                           VARIANT_BOOL *pfResult)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    cpp_bool result = FALSE;

    TRACE("(%p)->(%p %p)\n", This, pChild, pfResult);

    if(pChild) {
        HTMLElement *child;
        nsresult nsres;

        child = unsafe_impl_from_IHTMLElement(pChild);
        if(!child) {
            ERR("not our element\n");
            return E_FAIL;
        }

        nsres = nsIDOMNode_Contains(This->node.nsnode, child->node.nsnode, &result);
        assert(nsres == NS_OK);
    }

    *pfResult = variant_bool(result);
    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_sourceIndex(IHTMLElement *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_elem_source_index(This, p);
}

static HRESULT WINAPI HTMLElement_get_recordNumber(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_lang(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMHTMLElement_SetLang(This->html_element, &nsstr);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres)) {
        ERR("SetLang failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_lang(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_Init(&nsstr, NULL);
    nsres = nsIDOMHTMLElement_GetLang(This->html_element, &nsstr);
    return return_nsstr(nsres, &nsstr, p);
}

static HRESULT WINAPI HTMLElement_get_offsetLeft(IHTMLElement *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetOffsetLeft(This->html_element, p);
    if(NS_FAILED(nsres)) {
        ERR("GetOffsetLeft failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_offsetTop(IHTMLElement *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetOffsetTop(This->html_element, p);
    if(NS_FAILED(nsres)) {
        ERR("GetOffsetTop failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_offsetWidth(IHTMLElement *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetOffsetWidth(This->html_element, p);
    if(NS_FAILED(nsres)) {
        ERR("GetOffsetWidth failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_offsetHeight(IHTMLElement *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetOffsetHeight(This->html_element, p);
    if(NS_FAILED(nsres)) {
        ERR("GetOffsetHeight failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_offsetParent(IHTMLElement *iface, IHTMLElement **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsIDOMElement *nsparent;
    HTMLElement *parent;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetOffsetParent(This->html_element, &nsparent);
    if(NS_FAILED(nsres)) {
        ERR("GetOffsetParent failed: %08lx\n", nsres);
        return E_FAIL;
    }

    if(!nsparent) {
        *p = NULL;
        return S_OK;
    }

    hres = get_element(nsparent, &parent);
    nsIDOMElement_Release(nsparent);
    if(FAILED(hres))
        return hres;

    *p = &parent->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLElement_put_innerHTML(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString html_str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&html_str, v);
    nsres = nsIDOMHTMLElement_SetInnerHTML(This->html_element, &html_str);
    nsAString_Finish(&html_str);
    if(NS_FAILED(nsres)) {
        FIXME("SetInnerHtml failed %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_innerHTML(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString html_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_Init(&html_str, NULL);
    nsres = nsIDOMHTMLElement_GetInnerHTML(This->html_element, &html_str);
    return return_nsstr(nsres, &html_str, p);
}

static HRESULT WINAPI HTMLElement_put_innerText(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsIDOMNode *nschild, *tmp;
    nsIDOMText *text_node;
    nsAString text_str;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    while(1) {
        nsres = nsIDOMElement_GetLastChild(This->dom_element, &nschild);
        if(NS_FAILED(nsres)) {
            ERR("GetLastChild failed: %08lx\n", nsres);
            return E_FAIL;
        }
        if(!nschild)
            break;

        nsres = nsIDOMElement_RemoveChild(This->dom_element, nschild, &tmp);
        nsIDOMNode_Release(nschild);
        if(NS_FAILED(nsres)) {
            ERR("RemoveChild failed: %08lx\n", nsres);
            return E_FAIL;
        }
        nsIDOMNode_Release(tmp);
    }

    nsAString_InitDepend(&text_str, v);
    nsres = nsIDOMDocument_CreateTextNode(This->node.doc->dom_document, &text_str, &text_node);
    nsAString_Finish(&text_str);
    if(NS_FAILED(nsres)) {
        ERR("CreateTextNode failed: %08lx\n", nsres);
        return E_FAIL;
    }

    nsres = nsIDOMElement_AppendChild(This->dom_element, (nsIDOMNode*)text_node, &tmp);
    if(NS_FAILED(nsres)) {
        ERR("AppendChild failed: %08lx\n", nsres);
        return E_FAIL;
    }

    nsIDOMNode_Release(tmp);
    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_innerText(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_text(&This->node, p);
}

static HRESULT WINAPI HTMLElement_put_outerHTML(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    return replace_node_by_html(This->node.doc->dom_document, This->node.nsnode, v);
}

static HRESULT WINAPI HTMLElement_get_outerHTML(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsAString html_str;
    HRESULT hres;

    WARN("(%p)->(%p) semi-stub\n", This, p);

    nsAString_Init(&html_str, NULL);
    hres = nsnode_to_nsstring(This->node.nsnode, &html_str);
    if(SUCCEEDED(hres)) {
        const PRUnichar *html;

        nsAString_GetData(&html_str, &html);
        *p = SysAllocString(html);
        if(!*p)
            hres = E_OUTOFMEMORY;
    }

    nsAString_Finish(&html_str);

    TRACE("ret %s\n", debugstr_w(*p));
    return hres;
}

static HRESULT WINAPI HTMLElement_put_outerText(IHTMLElement *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsIDOMText *text_node;
    nsIDOMRange *range;
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(This->node.vtbl->is_settable && !This->node.vtbl->is_settable(&This->node, DISPID_IHTMLELEMENT_OUTERTEXT)) {
        WARN("Called on element that does not support setting the property.\n");
        return 0x800a0258; /* undocumented error code */
    }

    if(!This->node.doc->dom_document) {
        FIXME("NULL dom_document\n");
        return E_FAIL;
    }

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMDocument_CreateTextNode(This->node.doc->dom_document, &nsstr, &text_node);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres)) {
        ERR("CreateTextNode failed\n");
        return E_FAIL;
    }

    nsres = nsIDOMDocument_CreateRange(This->node.doc->dom_document, &range);
    if(NS_SUCCEEDED(nsres)) {
        nsres = nsIDOMRange_SelectNode(range, This->node.nsnode);
        if(NS_SUCCEEDED(nsres))
            nsres = nsIDOMRange_DeleteContents(range);
        if(NS_SUCCEEDED(nsres))
            nsres = nsIDOMRange_InsertNode(range, (nsIDOMNode*)text_node);
        if(NS_SUCCEEDED(nsres))
            nsres = nsIDOMRange_SelectNodeContents(range, This->node.nsnode);
        if(NS_SUCCEEDED(nsres))
            nsres = nsIDOMRange_DeleteContents(range);
        nsIDOMRange_Release(range);
    }
    nsIDOMText_Release(text_node);
    if(NS_FAILED(nsres)) {
        ERR("failed to set text: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_outerText(IHTMLElement *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    /* getter is the same as innerText */
    return IHTMLElement_get_innerText(&This->IHTMLElement_iface, p);
}

static HRESULT insert_adjacent_node(HTMLElement *This, const WCHAR *where, nsIDOMNode *nsnode, HTMLDOMNode **ret_node)
{
    nsIDOMNode *ret_nsnode;
    nsresult nsres;
    HRESULT hres = S_OK;

    if (!wcsicmp(where, L"beforebegin")) {
        nsIDOMNode *parent;

        nsres = nsIDOMNode_GetParentNode(This->node.nsnode, &parent);
        if(NS_FAILED(nsres))
            return E_FAIL;

        if(!parent)
            return E_INVALIDARG;

        nsres = nsIDOMNode_InsertBefore(parent, nsnode, This->node.nsnode, &ret_nsnode);
        nsIDOMNode_Release(parent);
    }else if(!wcsicmp(where, L"afterbegin")) {
        nsIDOMNode *first_child;

        nsres = nsIDOMNode_GetFirstChild(This->node.nsnode, &first_child);
        if(NS_FAILED(nsres))
            return E_FAIL;

        nsres = nsIDOMNode_InsertBefore(This->node.nsnode, nsnode, first_child, &ret_nsnode);
        if(NS_FAILED(nsres))
            return E_FAIL;

        if (first_child)
            nsIDOMNode_Release(first_child);
    }else if (!wcsicmp(where, L"beforeend")) {
        nsres = nsIDOMNode_AppendChild(This->node.nsnode, nsnode, &ret_nsnode);
    }else if (!wcsicmp(where, L"afterend")) {
        nsIDOMNode *next_sibling, *parent;

        nsres = nsIDOMNode_GetParentNode(This->node.nsnode, &parent);
        if(NS_FAILED(nsres))
            return E_FAIL;
        if(!parent)
            return E_INVALIDARG;

        nsres = nsIDOMNode_GetNextSibling(This->node.nsnode, &next_sibling);
        if(NS_SUCCEEDED(nsres)) {
            if(next_sibling) {
                nsres = nsIDOMNode_InsertBefore(parent, nsnode, next_sibling, &ret_nsnode);
                nsIDOMNode_Release(next_sibling);
            }else {
                nsres = nsIDOMNode_AppendChild(parent, nsnode, &ret_nsnode);
            }
        }

        nsIDOMNode_Release(parent);
    }else {
        ERR("invalid where: %s\n", debugstr_w(where));
        return E_INVALIDARG;
    }

    if (NS_FAILED(nsres))
        return E_FAIL;

    if(ret_node)
        hres = get_node(ret_nsnode, TRUE, ret_node);
    nsIDOMNode_Release(ret_nsnode);
    return hres;
}

static HRESULT WINAPI HTMLElement_insertAdjacentHTML(IHTMLElement *iface, BSTR where,
                                                     BSTR html)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsIDOMRange *range;
    nsIDOMNode *nsnode;
    nsAString ns_html;
    nsresult nsres;
    HRESULT hr;

    TRACE("(%p)->(%s %s)\n", This, debugstr_w(where), debugstr_w(html));

    if(!This->node.doc->dom_document) {
        WARN("NULL dom_document\n");
        return E_UNEXPECTED;
    }

    nsres = nsIDOMDocument_CreateRange(This->node.doc->dom_document, &range);
    if(NS_FAILED(nsres))
    {
        ERR("CreateRange failed: %08lx\n", nsres);
        return E_FAIL;
    }

    nsIDOMRange_SetStartBefore(range, This->node.nsnode);

    nsAString_InitDepend(&ns_html, html);
    nsres = nsIDOMRange_CreateContextualFragment(range, &ns_html, (nsIDOMDocumentFragment **)&nsnode);
    nsAString_Finish(&ns_html);
    nsIDOMRange_Release(range);

    if(NS_FAILED(nsres) || !nsnode)
    {
        ERR("CreateTextNode failed: %08lx\n", nsres);
        return E_FAIL;
    }

    hr = insert_adjacent_node(This, where, nsnode, NULL);
    nsIDOMNode_Release(nsnode);
    return hr;
}

static HRESULT WINAPI HTMLElement_insertAdjacentText(IHTMLElement *iface, BSTR where,
                                                     BSTR text)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsIDOMNode *nsnode;
    nsAString ns_text;
    nsresult nsres;
    HRESULT hr;

    TRACE("(%p)->(%s %s)\n", This, debugstr_w(where), debugstr_w(text));

    if(!This->node.doc->dom_document) {
        WARN("NULL dom_document\n");
        return E_UNEXPECTED;
    }


    nsAString_InitDepend(&ns_text, text);
    nsres = nsIDOMDocument_CreateTextNode(This->node.doc->dom_document, &ns_text, (nsIDOMText **)&nsnode);
    nsAString_Finish(&ns_text);

    if(NS_FAILED(nsres) || !nsnode)
    {
        ERR("CreateTextNode failed: %08lx\n", nsres);
        return E_FAIL;
    }

    hr = insert_adjacent_node(This, where, nsnode, NULL);
    nsIDOMNode_Release(nsnode);

    return hr;
}

static HRESULT WINAPI HTMLElement_get_parentTextEdit(IHTMLElement *iface, IHTMLElement **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_isTextEdit(IHTMLElement *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    *p = variant_bool(This->node.vtbl->is_text_edit && This->node.vtbl->is_text_edit(&This->node));
    return S_OK;
}

static HRESULT WINAPI HTMLElement_click(IHTMLElement *iface)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsresult nsres;

    TRACE("(%p)\n", This);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_Click(This->html_element);
    if(NS_FAILED(nsres)) {
        ERR("Click failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_filters(IHTMLElement *iface, IHTMLFiltersCollection **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    if(!p)
        return E_POINTER;

    return create_filters_collection(This, p);
}

static HRESULT WINAPI HTMLElement_put_ondragstart(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_DRAGSTART, &v);
}

static HRESULT WINAPI HTMLElement_get_ondragstart(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_DRAGSTART, p);
}

static HRESULT WINAPI HTMLElement_toString(IHTMLElement *iface, BSTR *String)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    HRESULT hres;
    VARIANT var;

    TRACE("(%p)->(%p)\n", This, String);

    if(!String)
        return E_INVALIDARG;

    hres = dispex_invoke(&This->node.event_target.dispex, (IDispatch*)&This->node.event_target.dispex.IDispatchEx_iface,
                         DISPID_VALUE, LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET, NULL, &var, NULL, NULL);
    if(SUCCEEDED(hres)) {
        assert(V_VT(&var) == VT_BSTR);
        *String = V_BSTR(&var);
    }
    return hres;
}

static HRESULT WINAPI HTMLElement_put_onbeforeupdate(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_onbeforeupdate(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_onafterupdate(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_onafterupdate(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_onerrorupdate(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_onerrorupdate(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_onrowexit(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_onrowexit(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_onrowenter(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_onrowenter(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_ondatasetchanged(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_ondatasetchanged(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_ondataavailable(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    FIXME("(%p)->(%s) semi-stub\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_DATAAVAILABLE, &v);
}

static HRESULT WINAPI HTMLElement_get_ondataavailable(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_DATAAVAILABLE, p);
}

static HRESULT WINAPI HTMLElement_put_ondatasetcomplete(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_ondatasetcomplete(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_put_onfilterchange(IHTMLElement *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_onfilterchange(IHTMLElement *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement_get_children(IHTMLElement *iface, IDispatch **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);
    nsIDOMNodeList *nsnode_list;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    nsres = nsIDOMNode_GetChildNodes(This->node.nsnode, &nsnode_list);
    if(NS_FAILED(nsres)) {
        ERR("GetChildNodes failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = (IDispatch*)create_collection_from_nodelist(nsnode_list, This->node.doc);

    nsIDOMNodeList_Release(nsnode_list);
    return S_OK;
}

static HRESULT WINAPI HTMLElement_get_all(IHTMLElement *iface, IDispatch **p)
{
    HTMLElement *This = impl_from_IHTMLElement(iface);

    TRACE("(%p)->(%p)\n", This, p);

    *p = (IDispatch*)create_all_collection(&This->node, FALSE);
    return S_OK;
}

static const IHTMLElementVtbl HTMLElementVtbl = {
    HTMLElement_QueryInterface,
    HTMLElement_AddRef,
    HTMLElement_Release,
    HTMLElement_GetTypeInfoCount,
    HTMLElement_GetTypeInfo,
    HTMLElement_GetIDsOfNames,
    HTMLElement_Invoke,
    HTMLElement_setAttribute,
    HTMLElement_getAttribute,
    HTMLElement_removeAttribute,
    HTMLElement_put_className,
    HTMLElement_get_className,
    HTMLElement_put_id,
    HTMLElement_get_id,
    HTMLElement_get_tagName,
    HTMLElement_get_parentElement,
    HTMLElement_get_style,
    HTMLElement_put_onhelp,
    HTMLElement_get_onhelp,
    HTMLElement_put_onclick,
    HTMLElement_get_onclick,
    HTMLElement_put_ondblclick,
    HTMLElement_get_ondblclick,
    HTMLElement_put_onkeydown,
    HTMLElement_get_onkeydown,
    HTMLElement_put_onkeyup,
    HTMLElement_get_onkeyup,
    HTMLElement_put_onkeypress,
    HTMLElement_get_onkeypress,
    HTMLElement_put_onmouseout,
    HTMLElement_get_onmouseout,
    HTMLElement_put_onmouseover,
    HTMLElement_get_onmouseover,
    HTMLElement_put_onmousemove,
    HTMLElement_get_onmousemove,
    HTMLElement_put_onmousedown,
    HTMLElement_get_onmousedown,
    HTMLElement_put_onmouseup,
    HTMLElement_get_onmouseup,
    HTMLElement_get_document,
    HTMLElement_put_title,
    HTMLElement_get_title,
    HTMLElement_put_language,
    HTMLElement_get_language,
    HTMLElement_put_onselectstart,
    HTMLElement_get_onselectstart,
    HTMLElement_scrollIntoView,
    HTMLElement_contains,
    HTMLElement_get_sourceIndex,
    HTMLElement_get_recordNumber,
    HTMLElement_put_lang,
    HTMLElement_get_lang,
    HTMLElement_get_offsetLeft,
    HTMLElement_get_offsetTop,
    HTMLElement_get_offsetWidth,
    HTMLElement_get_offsetHeight,
    HTMLElement_get_offsetParent,
    HTMLElement_put_innerHTML,
    HTMLElement_get_innerHTML,
    HTMLElement_put_innerText,
    HTMLElement_get_innerText,
    HTMLElement_put_outerHTML,
    HTMLElement_get_outerHTML,
    HTMLElement_put_outerText,
    HTMLElement_get_outerText,
    HTMLElement_insertAdjacentHTML,
    HTMLElement_insertAdjacentText,
    HTMLElement_get_parentTextEdit,
    HTMLElement_get_isTextEdit,
    HTMLElement_click,
    HTMLElement_get_filters,
    HTMLElement_put_ondragstart,
    HTMLElement_get_ondragstart,
    HTMLElement_toString,
    HTMLElement_put_onbeforeupdate,
    HTMLElement_get_onbeforeupdate,
    HTMLElement_put_onafterupdate,
    HTMLElement_get_onafterupdate,
    HTMLElement_put_onerrorupdate,
    HTMLElement_get_onerrorupdate,
    HTMLElement_put_onrowexit,
    HTMLElement_get_onrowexit,
    HTMLElement_put_onrowenter,
    HTMLElement_get_onrowenter,
    HTMLElement_put_ondatasetchanged,
    HTMLElement_get_ondatasetchanged,
    HTMLElement_put_ondataavailable,
    HTMLElement_get_ondataavailable,
    HTMLElement_put_ondatasetcomplete,
    HTMLElement_get_ondatasetcomplete,
    HTMLElement_put_onfilterchange,
    HTMLElement_get_onfilterchange,
    HTMLElement_get_children,
    HTMLElement_get_all
};

HTMLElement *unsafe_impl_from_IHTMLElement(IHTMLElement *iface)
{
    return iface->lpVtbl == &HTMLElementVtbl ? impl_from_IHTMLElement(iface) : NULL;
}

static inline HTMLElement *impl_from_IHTMLElement2(IHTMLElement2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IHTMLElement2_iface);
}

static HRESULT WINAPI HTMLElement2_QueryInterface(IHTMLElement2 *iface,
                                                  REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI HTMLElement2_AddRef(IHTMLElement2 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI HTMLElement2_Release(IHTMLElement2 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI HTMLElement2_GetTypeInfoCount(IHTMLElement2 *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLElement2_GetTypeInfo(IHTMLElement2 *iface, UINT iTInfo,
                                               LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLElement2_GetIDsOfNames(IHTMLElement2 *iface, REFIID riid,
                                                LPOLESTR *rgszNames, UINT cNames,
                                                LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLElement2_Invoke(IHTMLElement2 *iface, DISPID dispIdMember,
                            REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                            VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLElement2_get_scopeName(IHTMLElement2 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_setCapture(IHTMLElement2 *iface, VARIANT_BOOL containerCapture)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%x)\n", This, containerCapture);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_releaseCapture(IHTMLElement2 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_onlosecapture(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onlosecapture(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_componentFromPoint(IHTMLElement2 *iface,
                                                      LONG x, LONG y, BSTR *component)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%ld %ld %p)\n", This, x, y, component);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_doScroll(IHTMLElement2 *iface, VARIANT component)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&component));

    if(!This->node.doc->content_ready || !This->node.doc->doc_obj->in_place_active)
        return E_PENDING;

    WARN("stub\n");
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_put_onscroll(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_SCROLL, &v);
}

static HRESULT WINAPI HTMLElement2_get_onscroll(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_SCROLL, p);
}

static HRESULT WINAPI HTMLElement2_put_ondrag(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_DRAG, &v);
}

static HRESULT WINAPI HTMLElement2_get_ondrag(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_DRAG, p);
}

static HRESULT WINAPI HTMLElement2_put_ondragend(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_ondragend(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_ondragenter(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_ondragenter(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_ondragover(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_ondragover(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_ondragleave(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_ondragleave(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_ondrop(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_ondrop(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_onbeforecut(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onbeforecut(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_oncut(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_oncut(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_onbeforecopy(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onbeforecopy(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_oncopy(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_oncopy(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_onbeforepaste(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onbeforepaste(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_onpaste(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_PASTE, &v);
}

static HRESULT WINAPI HTMLElement2_get_onpaste(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_PASTE, p);
}

static HRESULT WINAPI HTMLElement2_get_currentStyle(IHTMLElement2 *iface, IHTMLCurrentStyle **p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return HTMLCurrentStyle_Create(This, p);
}

static HRESULT WINAPI HTMLElement2_put_onpropertychange(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onpropertychange(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_getClientRects(IHTMLElement2 *iface, IHTMLRectCollection **pRectCol)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsIDOMClientRectList *rect_list;
    HTMLRectCollection *rects;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, pRectCol);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetClientRects(This->dom_element, &rect_list);
    if(NS_FAILED(nsres)) {
        WARN("GetClientRects failed: %08lx\n", nsres);
        return map_nsresult(nsres);
    }

    rects = calloc(1, sizeof(*rects));
    if(!rects) {
        nsIDOMClientRectList_Release(rect_list);
        return E_OUTOFMEMORY;
    }

    rects->IHTMLRectCollection_iface.lpVtbl = &HTMLRectCollectionVtbl;
    rects->ref = 1;
    rects->rect_list = rect_list;
    rects->window = get_inner_window(This->node.doc);
    IHTMLWindow2_AddRef(&rects->window->base.IHTMLWindow2_iface);

    init_dispatch(&rects->dispex, (IUnknown*)&rects->IHTMLRectCollection_iface, &HTMLRectCollection_dispex,
                  rects->window, dispex_compat_mode(&This->node.event_target.dispex));

    *pRectCol = &rects->IHTMLRectCollection_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_getBoundingClientRect(IHTMLElement2 *iface, IHTMLRect **pRect)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsIDOMClientRect *nsrect;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, pRect);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetBoundingClientRect(This->dom_element, &nsrect);
    if(NS_FAILED(nsres) || !nsrect) {
        ERR("GetBoindingClientRect failed: %08lx\n", nsres);
        return E_FAIL;
    }

    hres = create_html_rect(nsrect, get_inner_window(This->node.doc), dispex_compat_mode(&This->node.event_target.dispex), pRect);

    nsIDOMClientRect_Release(nsrect);
    return hres;
}

static HRESULT WINAPI HTMLElement2_setExpression(IHTMLElement2 *iface, BSTR propname,
                                                 BSTR expression, BSTR language)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%s %s %s)\n", This, debugstr_w(propname), debugstr_w(expression),
          debugstr_w(language));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_getExpression(IHTMLElement2 *iface, BSTR propname,
                                                 VARIANT *expression)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%s %p)\n", This, debugstr_w(propname), expression);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_removeExpression(IHTMLElement2 *iface, BSTR propname,
                                                    VARIANT_BOOL *pfSuccess)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%s %p)\n", This, debugstr_w(propname), pfSuccess);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_tabIndex(IHTMLElement2 *iface, short v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%d)\n", This, v);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_SetTabIndex(This->html_element, v);
    if(NS_FAILED(nsres))
        ERR("GetTabIndex failed: %08lx\n", nsres);

    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_tabIndex(IHTMLElement2 *iface, short *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    LONG index;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetTabIndex(This->html_element, &index);
    if(NS_FAILED(nsres)) {
        ERR("GetTabIndex failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *p = index;
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_focus(IHTMLElement2 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)\n", This);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_Focus(This->html_element);
    if(NS_FAILED(nsres))
        ERR("Focus failed: %08lx\n", nsres);

    return S_OK;
}

static HRESULT WINAPI HTMLElement2_put_accessKey(IHTMLElement2 *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMHTMLElement_SetAccessKey(This->html_element, &nsstr);
    nsAString_Finish(&nsstr);
    return map_nsresult(nsres);
}

static HRESULT WINAPI HTMLElement2_get_accessKey(IHTMLElement2 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&nsstr, NULL);
    nsres = nsIDOMHTMLElement_GetAccessKey(This->html_element, &nsstr);
    return return_nsstr(nsres, &nsstr, p);
}

static HRESULT WINAPI HTMLElement2_put_onblur(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_BLUR, &v);
}

static HRESULT WINAPI HTMLElement2_get_onblur(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_BLUR, p);
}

static HRESULT WINAPI HTMLElement2_put_onfocus(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_FOCUS, &v);
}

static HRESULT WINAPI HTMLElement2_get_onfocus(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_FOCUS, p);
}

static HRESULT WINAPI HTMLElement2_put_onresize(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_RESIZE, &v);
}

static HRESULT WINAPI HTMLElement2_get_onresize(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_RESIZE, p);
}

static HRESULT WINAPI HTMLElement2_blur(IHTMLElement2 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)\n", This);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_Blur(This->html_element);
    if(NS_FAILED(nsres)) {
        ERR("Blur failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement2_addFilter(IHTMLElement2 *iface, IUnknown *pUnk)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, pUnk);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_removeFilter(IHTMLElement2 *iface, IUnknown *pUnk)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, pUnk);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_clientHeight(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("Unimplemented for comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetClientHeight(This->dom_element, p);
    assert(nsres == NS_OK);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_clientWidth(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetClientWidth(This->dom_element, p);
    assert(nsres == NS_OK);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_clientTop(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetClientTop(This->dom_element, p);
    assert(nsres == NS_OK);

    TRACE("*p = %ld\n", *p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_clientLeft(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetClientLeft(This->dom_element, p);
    assert(nsres == NS_OK);

    TRACE("*p = %ld\n", *p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_attachEvent(IHTMLElement2 *iface, BSTR event,
                                               IDispatch *pDisp, VARIANT_BOOL *pfResult)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s %p %p)\n", This, debugstr_w(event), pDisp, pfResult);

    return attach_event(&This->node.event_target, event, pDisp, pfResult);
}

static HRESULT WINAPI HTMLElement2_detachEvent(IHTMLElement2 *iface, BSTR event, IDispatch *pDisp)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(event), pDisp);

    return detach_event(&This->node.event_target, event, pDisp);
}

static HRESULT WINAPI HTMLElement2_get_readyState(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    BSTR str;

    TRACE("(%p)->(%p)\n", This, p);

    if(This->node.vtbl->get_readystate) {
        HRESULT hres;

        hres = This->node.vtbl->get_readystate(&This->node, &str);
        if(FAILED(hres))
            return hres;
    }else {
        str = SysAllocString(L"complete");
        if(!str)
            return E_OUTOFMEMORY;
    }

    V_VT(p) = VT_BSTR;
    V_BSTR(p) = str;
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_put_onreadystatechange(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_READYSTATECHANGE, &v);
}

static HRESULT WINAPI HTMLElement2_get_onreadystatechange(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_READYSTATECHANGE, p);
}

static HRESULT WINAPI HTMLElement2_put_onrowsdelete(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onrowsdelete(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_onrowsinserted(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onrowsinserted(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_oncellchange(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_oncellchange(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_dir(IHTMLElement2 *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return S_OK;
    }

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMHTMLElement_SetDir(This->html_element, &nsstr);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres)) {
        ERR("SetDir failed: %08lx\n", nsres);
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_dir(IHTMLElement2 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsAString dir_str;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        if(This->dom_element)
            FIXME("non-HTML element\n");
        *p = NULL;
        return S_OK;
    }

    nsAString_Init(&dir_str, NULL);
    nsres = nsIDOMHTMLElement_GetDir(This->html_element, &dir_str);
    return return_nsstr(nsres, &dir_str, p);
}

static HRESULT WINAPI HTMLElement2_createControlRange(IHTMLElement2 *iface, IDispatch **range)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, range);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_scrollHeight(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetScrollHeight(This->dom_element, p);
    assert(nsres == NS_OK);
    TRACE("*p = %ld\n", *p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_scrollWidth(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetScrollWidth(This->dom_element, p);
    assert(nsres == NS_OK);

    TRACE("*p = %ld\n", *p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_put_scrollTop(IHTMLElement2 *iface, LONG v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%ld)\n", This, v);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsIDOMElement_SetScrollTop(This->dom_element, v);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_scrollTop(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetScrollTop(This->dom_element, p);
    assert(nsres == NS_OK);

    TRACE("*p = %ld\n", *p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_put_scrollLeft(IHTMLElement2 *iface, LONG v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%ld)\n", This, v);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsIDOMElement_SetScrollLeft(This->dom_element, v);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_scrollLeft(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!p)
        return E_INVALIDARG;

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMElement_GetScrollLeft(This->dom_element, p);
    assert(nsres == NS_OK);
    TRACE("*p = %ld\n", *p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_clearAttributes(IHTMLElement2 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_mergeAttributes(IHTMLElement2 *iface, IHTMLElement *mergeThis)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, mergeThis);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_oncontextmenu(IHTMLElement2 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->()\n", This);

    return set_node_event(&This->node, EVENTID_CONTEXTMENU, &v);
}

static HRESULT WINAPI HTMLElement2_get_oncontextmenu(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_CONTEXTMENU, p);
}

static HRESULT WINAPI HTMLElement2_insertAdjacentElement(IHTMLElement2 *iface, BSTR where,
        IHTMLElement *insertedElement, IHTMLElement **inserted)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    HTMLDOMNode *ret_node;
    HTMLElement *elem;
    HRESULT hres;

    TRACE("(%p)->(%s %p %p)\n", This, debugstr_w(where), insertedElement, inserted);

    elem = unsafe_impl_from_IHTMLElement(insertedElement);
    if(!elem)
        return E_INVALIDARG;

    hres = insert_adjacent_node(This, where, elem->node.nsnode, &ret_node);
    if(FAILED(hres))
        return hres;

    hres = IHTMLDOMNode_QueryInterface(&ret_node->IHTMLDOMNode_iface, &IID_IHTMLElement, (void**)inserted);
    IHTMLDOMNode_Release(&ret_node->IHTMLDOMNode_iface);
    return hres;
}

static HRESULT WINAPI HTMLElement2_applyElement(IHTMLElement2 *iface, IHTMLElement *apply,
                                                BSTR where, IHTMLElement **applied)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p %s %p)\n", This, apply, debugstr_w(where), applied);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_getAdjacentText(IHTMLElement2 *iface, BSTR where, BSTR *text)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%s %p)\n", This, debugstr_w(where), text);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_replaceAdjacentText(IHTMLElement2 *iface, BSTR where,
                                                       BSTR newText, BSTR *oldText)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%s %s %p)\n", This, debugstr_w(where), debugstr_w(newText), oldText);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_canHandleChildren(IHTMLElement2 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_addBehavior(IHTMLElement2 *iface, BSTR bstrUrl,
                                               VARIANT *pvarFactory, LONG *pCookie)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%s %p %p)\n", This, debugstr_w(bstrUrl), pvarFactory, pCookie);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_removeBehavior(IHTMLElement2 *iface, LONG cookie,
                                                  VARIANT_BOOL *pfResult)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%ld %p)\n", This, cookie, pfResult);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_runtimeStyle(IHTMLElement2 *iface, IHTMLStyle **p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);

    FIXME("(%p)->(%p): hack\n", This, p);

    /* We can't implement correct behavior on top of Gecko (although we could
       try a bit harder). Making runtimeStyle behave like regular style is
       enough for most use cases. */
    if(!This->runtime_style) {
        HRESULT hres;

        hres = HTMLStyle_Create(This, &This->runtime_style);
        if(FAILED(hres))
            return hres;
    }

    *p = &This->runtime_style->IHTMLStyle_iface;
    IHTMLStyle_AddRef(*p);
    return S_OK;
}

static HRESULT WINAPI HTMLElement2_get_behaviorUrns(IHTMLElement2 *iface, IDispatch **p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_tagUrn(IHTMLElement2 *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_tagUrn(IHTMLElement2 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_put_onbeforeeditfocus(IHTMLElement2 *iface, VARIANT vv)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_onbeforeeditfocus(IHTMLElement2 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_get_readyStateValue(IHTMLElement2 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement2_getElementsByTagName(IHTMLElement2 *iface, BSTR v,
                                                       IHTMLElementCollection **pelColl)
{
    HTMLElement *This = impl_from_IHTMLElement2(iface);
    nsIDOMHTMLCollection *nscol;
    nsAString tag_str;
    nsresult nsres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(v), pelColl);

    if(!This->dom_element) {
        *pelColl = create_collection_from_htmlcol(NULL, This->node.doc, dispex_compat_mode(&This->node.event_target.dispex));
        return S_OK;
    }

    nsAString_InitDepend(&tag_str, v);
    nsres = nsIDOMElement_GetElementsByTagName(This->dom_element, &tag_str, &nscol);
    nsAString_Finish(&tag_str);
    if(NS_FAILED(nsres)) {
        ERR("GetElementByTagName failed: %08lx\n", nsres);
        return E_FAIL;
    }

    *pelColl = create_collection_from_htmlcol(nscol, This->node.doc, dispex_compat_mode(&This->node.event_target.dispex));
    nsIDOMHTMLCollection_Release(nscol);
    return S_OK;
}

static const IHTMLElement2Vtbl HTMLElement2Vtbl = {
    HTMLElement2_QueryInterface,
    HTMLElement2_AddRef,
    HTMLElement2_Release,
    HTMLElement2_GetTypeInfoCount,
    HTMLElement2_GetTypeInfo,
    HTMLElement2_GetIDsOfNames,
    HTMLElement2_Invoke,
    HTMLElement2_get_scopeName,
    HTMLElement2_setCapture,
    HTMLElement2_releaseCapture,
    HTMLElement2_put_onlosecapture,
    HTMLElement2_get_onlosecapture,
    HTMLElement2_componentFromPoint,
    HTMLElement2_doScroll,
    HTMLElement2_put_onscroll,
    HTMLElement2_get_onscroll,
    HTMLElement2_put_ondrag,
    HTMLElement2_get_ondrag,
    HTMLElement2_put_ondragend,
    HTMLElement2_get_ondragend,
    HTMLElement2_put_ondragenter,
    HTMLElement2_get_ondragenter,
    HTMLElement2_put_ondragover,
    HTMLElement2_get_ondragover,
    HTMLElement2_put_ondragleave,
    HTMLElement2_get_ondragleave,
    HTMLElement2_put_ondrop,
    HTMLElement2_get_ondrop,
    HTMLElement2_put_onbeforecut,
    HTMLElement2_get_onbeforecut,
    HTMLElement2_put_oncut,
    HTMLElement2_get_oncut,
    HTMLElement2_put_onbeforecopy,
    HTMLElement2_get_onbeforecopy,
    HTMLElement2_put_oncopy,
    HTMLElement2_get_oncopy,
    HTMLElement2_put_onbeforepaste,
    HTMLElement2_get_onbeforepaste,
    HTMLElement2_put_onpaste,
    HTMLElement2_get_onpaste,
    HTMLElement2_get_currentStyle,
    HTMLElement2_put_onpropertychange,
    HTMLElement2_get_onpropertychange,
    HTMLElement2_getClientRects,
    HTMLElement2_getBoundingClientRect,
    HTMLElement2_setExpression,
    HTMLElement2_getExpression,
    HTMLElement2_removeExpression,
    HTMLElement2_put_tabIndex,
    HTMLElement2_get_tabIndex,
    HTMLElement2_focus,
    HTMLElement2_put_accessKey,
    HTMLElement2_get_accessKey,
    HTMLElement2_put_onblur,
    HTMLElement2_get_onblur,
    HTMLElement2_put_onfocus,
    HTMLElement2_get_onfocus,
    HTMLElement2_put_onresize,
    HTMLElement2_get_onresize,
    HTMLElement2_blur,
    HTMLElement2_addFilter,
    HTMLElement2_removeFilter,
    HTMLElement2_get_clientHeight,
    HTMLElement2_get_clientWidth,
    HTMLElement2_get_clientTop,
    HTMLElement2_get_clientLeft,
    HTMLElement2_attachEvent,
    HTMLElement2_detachEvent,
    HTMLElement2_get_readyState,
    HTMLElement2_put_onreadystatechange,
    HTMLElement2_get_onreadystatechange,
    HTMLElement2_put_onrowsdelete,
    HTMLElement2_get_onrowsdelete,
    HTMLElement2_put_onrowsinserted,
    HTMLElement2_get_onrowsinserted,
    HTMLElement2_put_oncellchange,
    HTMLElement2_get_oncellchange,
    HTMLElement2_put_dir,
    HTMLElement2_get_dir,
    HTMLElement2_createControlRange,
    HTMLElement2_get_scrollHeight,
    HTMLElement2_get_scrollWidth,
    HTMLElement2_put_scrollTop,
    HTMLElement2_get_scrollTop,
    HTMLElement2_put_scrollLeft,
    HTMLElement2_get_scrollLeft,
    HTMLElement2_clearAttributes,
    HTMLElement2_mergeAttributes,
    HTMLElement2_put_oncontextmenu,
    HTMLElement2_get_oncontextmenu,
    HTMLElement2_insertAdjacentElement,
    HTMLElement2_applyElement,
    HTMLElement2_getAdjacentText,
    HTMLElement2_replaceAdjacentText,
    HTMLElement2_get_canHandleChildren,
    HTMLElement2_addBehavior,
    HTMLElement2_removeBehavior,
    HTMLElement2_get_runtimeStyle,
    HTMLElement2_get_behaviorUrns,
    HTMLElement2_put_tagUrn,
    HTMLElement2_get_tagUrn,
    HTMLElement2_put_onbeforeeditfocus,
    HTMLElement2_get_onbeforeeditfocus,
    HTMLElement2_get_readyStateValue,
    HTMLElement2_getElementsByTagName,
};

static inline HTMLElement *impl_from_IHTMLElement3(IHTMLElement3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IHTMLElement3_iface);
}

static HRESULT WINAPI HTMLElement3_QueryInterface(IHTMLElement3 *iface,
                                                  REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI HTMLElement3_AddRef(IHTMLElement3 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI HTMLElement3_Release(IHTMLElement3 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI HTMLElement3_GetTypeInfoCount(IHTMLElement3 *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLElement3_GetTypeInfo(IHTMLElement3 *iface, UINT iTInfo,
                                               LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLElement3_GetIDsOfNames(IHTMLElement3 *iface, REFIID riid,
                                                LPOLESTR *rgszNames, UINT cNames,
                                                LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLElement3_Invoke(IHTMLElement3 *iface, DISPID dispIdMember,
                            REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                            VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLElement3_mergeAttributes(IHTMLElement3 *iface, IHTMLElement *mergeThis, VARIANT *pvarFlags)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p %p)\n", This, mergeThis, pvarFlags);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_isMultiLine(IHTMLElement3 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_canHaveHTML(IHTMLElement3 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onlayoutcomplete(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onlayoutcomplete(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onpage(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onpage(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_inflateBlock(IHTMLElement3 *iface, VARIANT_BOOL v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%x)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_inflateBlock(IHTMLElement3 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onbeforedeactivate(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onbeforedeactivate(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_setActive(IHTMLElement3 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_contentEditable(IHTMLElement3 *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    nsresult nsres;
    nsAString str;

    TRACE("(%p)->(%s)\n", This, debugstr_w(v));

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&str, v);
    nsres = nsIDOMHTMLElement_SetContentEditable(This->html_element, &str);
    nsAString_Finish(&str);

    if (NS_FAILED(nsres)){
        ERR("SetContentEditable(%s) failed!\n", debugstr_w(v));
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT WINAPI HTMLElement3_get_contentEditable(IHTMLElement3 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    nsresult nsres;
    nsAString str;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsAString_Init(&str, NULL);
    nsres = nsIDOMHTMLElement_GetContentEditable(This->html_element, &str);
    return return_nsstr(nsres, &str, p);
}

static HRESULT WINAPI HTMLElement3_get_isContentEditable(IHTMLElement3 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    nsresult nsres;
    cpp_bool r;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetIsContentEditable(This->html_element, &r);
    *p = variant_bool(NS_SUCCEEDED(nsres) && r);
    return S_OK;
}

static HRESULT WINAPI HTMLElement3_put_hideFocus(IHTMLElement3 *iface, VARIANT_BOOL v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%x)\n", This, v);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_hideFocus(IHTMLElement3 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_disabled(IHTMLElement3 *iface, VARIANT_BOOL v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);

    TRACE("(%p)->(%x)\n", This, v);

    if(This->node.vtbl->put_disabled)
        return This->node.vtbl->put_disabled(&This->node, v);

    if(!v) return element_remove_attribute(This, L"disabled");
    return elem_string_attr_setter(This, L"disabled", L"");
}

static HRESULT WINAPI HTMLElement3_get_disabled(IHTMLElement3 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);

    TRACE("(%p)->(%p)\n", This, p);

    if(This->node.vtbl->get_disabled)
        return This->node.vtbl->get_disabled(&This->node, p);

    *p = variant_bool(element_has_attribute(This, L"disabled"));
    return S_OK;
}

static HRESULT WINAPI HTMLElement3_get_isDisabled(IHTMLElement3 *iface, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onmove(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onmove(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_oncontrolselect(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_oncontrolselect(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_fireEvent(IHTMLElement3 *iface, BSTR bstrEventName,
        VARIANT *pvarEventObject, VARIANT_BOOL *pfCancelled)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);

    TRACE("(%p)->(%s %s %p)\n", This, debugstr_w(bstrEventName), debugstr_variant(pvarEventObject),
          pfCancelled);

    return fire_event(&This->node, bstrEventName, pvarEventObject, pfCancelled);
}

static HRESULT WINAPI HTMLElement3_put_onresizestart(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onresizestart(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onresizeend(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onresizeend(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onmovestart(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onmovestart(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onmoveend(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onmoveend(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onmousecenter(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onmousecenter(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onmouseleave(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onmouseleave(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_onactivate(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_onactivate(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_put_ondeactivate(IHTMLElement3 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_ondeactivate(IHTMLElement3 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_dragDrop(IHTMLElement3 *iface, VARIANT_BOOL *pfRet)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, pfRet);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement3_get_glyphMode(IHTMLElement3 *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLElement3(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static const IHTMLElement3Vtbl HTMLElement3Vtbl = {
    HTMLElement3_QueryInterface,
    HTMLElement3_AddRef,
    HTMLElement3_Release,
    HTMLElement3_GetTypeInfoCount,
    HTMLElement3_GetTypeInfo,
    HTMLElement3_GetIDsOfNames,
    HTMLElement3_Invoke,
    HTMLElement3_mergeAttributes,
    HTMLElement3_get_isMultiLine,
    HTMLElement3_get_canHaveHTML,
    HTMLElement3_put_onlayoutcomplete,
    HTMLElement3_get_onlayoutcomplete,
    HTMLElement3_put_onpage,
    HTMLElement3_get_onpage,
    HTMLElement3_put_inflateBlock,
    HTMLElement3_get_inflateBlock,
    HTMLElement3_put_onbeforedeactivate,
    HTMLElement3_get_onbeforedeactivate,
    HTMLElement3_setActive,
    HTMLElement3_put_contentEditable,
    HTMLElement3_get_contentEditable,
    HTMLElement3_get_isContentEditable,
    HTMLElement3_put_hideFocus,
    HTMLElement3_get_hideFocus,
    HTMLElement3_put_disabled,
    HTMLElement3_get_disabled,
    HTMLElement3_get_isDisabled,
    HTMLElement3_put_onmove,
    HTMLElement3_get_onmove,
    HTMLElement3_put_oncontrolselect,
    HTMLElement3_get_oncontrolselect,
    HTMLElement3_fireEvent,
    HTMLElement3_put_onresizestart,
    HTMLElement3_get_onresizestart,
    HTMLElement3_put_onresizeend,
    HTMLElement3_get_onresizeend,
    HTMLElement3_put_onmovestart,
    HTMLElement3_get_onmovestart,
    HTMLElement3_put_onmoveend,
    HTMLElement3_get_onmoveend,
    HTMLElement3_put_onmousecenter,
    HTMLElement3_get_onmousecenter,
    HTMLElement3_put_onmouseleave,
    HTMLElement3_get_onmouseleave,
    HTMLElement3_put_onactivate,
    HTMLElement3_get_onactivate,
    HTMLElement3_put_ondeactivate,
    HTMLElement3_get_ondeactivate,
    HTMLElement3_dragDrop,
    HTMLElement3_get_glyphMode
};

static inline HTMLElement *impl_from_IHTMLElement4(IHTMLElement4 *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IHTMLElement4_iface);
}

static HRESULT WINAPI HTMLElement4_QueryInterface(IHTMLElement4 *iface,
        REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI HTMLElement4_AddRef(IHTMLElement4 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI HTMLElement4_Release(IHTMLElement4 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI HTMLElement4_GetTypeInfoCount(IHTMLElement4 *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLElement4_GetTypeInfo(IHTMLElement4 *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLElement4_GetIDsOfNames(IHTMLElement4 *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLElement4_Invoke(IHTMLElement4 *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLElement4_put_onmousewheel(IHTMLElement4 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_MOUSEWHEEL, &v);
}

static HRESULT WINAPI HTMLElement4_get_onmousewheel(IHTMLElement4 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_MOUSEWHEEL, p);
}

static HRESULT WINAPI HTMLElement4_normalize(IHTMLElement4 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement4_getAttributeNode(IHTMLElement4 *iface, BSTR bstrname, IHTMLDOMAttribute **ppAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    HTMLAttributeCollection *attrs;
    HRESULT hres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(bstrname), ppAttribute);

    hres = HTMLElement_get_attr_col(&This->node, &attrs);
    if(FAILED(hres))
        return hres;

    hres = IHTMLAttributeCollection2_getNamedItem(&attrs->IHTMLAttributeCollection2_iface, bstrname, ppAttribute);
    IHTMLAttributeCollection_Release(&attrs->IHTMLAttributeCollection_iface);
    return hres;
}

static HRESULT WINAPI HTMLElement4_setAttributeNode(IHTMLElement4 *iface, IHTMLDOMAttribute *pattr,
        IHTMLDOMAttribute **ppretAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    nsIDOMAttr *nsattr, *oldnsattr = NULL;
    HTMLDOMAttribute *attr, *replace;
    HTMLAttributeCollection *attrs;
    DISPID dispid;
    HRESULT hres;

    TRACE("(%p)->(%p %p)\n", This, pattr, ppretAttribute);

    attr = unsafe_impl_from_IHTMLDOMAttribute(pattr);
    if(!attr)
        return E_INVALIDARG;

    if(attr->elem) {
        WARN("Tried to set already attached attribute.\n");
        return E_INVALIDARG;
    }

    hres = HTMLElement_get_attr_col(&This->node, &attrs);
    if(FAILED(hres))
        return hres;

    nsattr = (nsIDOMAttr*)attr->node.nsnode;
    if(nsattr && !attrs->nsattrs) {
        FIXME("Attribute has proper node but no NS collection for it.\n");
        IHTMLAttributeCollection_Release(&attrs->IHTMLAttributeCollection_iface);
        return E_NOTIMPL;
    }

    if(nsattr) {
        if(NS_FAILED(nsIDOMMozNamedAttrMap_SetNamedItem(attrs->nsattrs, nsattr, &oldnsattr))) {
            IHTMLAttributeCollection_Release(&attrs->IHTMLAttributeCollection_iface);
            return E_FAIL;
        }
        dispid = get_dispid_for_nsattr(This, nsattr);
        replace = NULL;
        if(oldnsattr) {
            replace = find_attr_in_list(attrs, 0, oldnsattr, NULL);
            nsIDOMAttr_Release(oldnsattr);
        }
    }else {
        hres = IDispatchEx_GetDispID(&This->node.event_target.dispex.IDispatchEx_iface,
                attr->name, fdexNameCaseInsensitive|fdexNameEnsure, &dispid);
        if(FAILED(hres)) {
            IHTMLAttributeCollection_Release(&attrs->IHTMLAttributeCollection_iface);
            return hres;
        }
        replace = find_attr_in_list(attrs, dispid, NULL, NULL);
    }

    if(replace) {
        if(!nsattr) {
            hres = get_elem_attr_value_by_dispid(This, dispid, &replace->value);
            if(FAILED(hres)) {
                WARN("could not get attr value: %08lx\n", hres);
                V_VT(&replace->value) = VT_EMPTY;
            }
            if(!replace->name) {
                replace->name = attr->name;
                attr->name = NULL;
            }
        }
        list_add_head(&replace->entry, &attr->entry);
        list_remove(&replace->entry);
        replace->elem = NULL;
    }else {
        list_add_tail(&attrs->attrs, &attr->entry);
    }

    IHTMLDOMAttribute_AddRef(&attr->IHTMLDOMAttribute_iface);
    attr->elem = This;
    attr->dispid = dispid;

    IHTMLAttributeCollection_Release(&attrs->IHTMLAttributeCollection_iface);

    if(!nsattr) {
        hres = set_elem_attr_value_by_dispid(This, dispid, &attr->value);
        if(FAILED(hres))
            WARN("Could not set attribute value: %08lx\n", hres);
        VariantClear(&attr->value);
    }

    *ppretAttribute = replace ? &replace->IHTMLDOMAttribute_iface : NULL;
    return S_OK;
}

static HRESULT WINAPI HTMLElement4_removeAttributeNode(IHTMLElement4 *iface, IHTMLDOMAttribute *pattr,
        IHTMLDOMAttribute **ppretAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);
    FIXME("(%p)->(%p %p)\n", This, pattr, ppretAttribute);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement4_put_onbeforeactivate(IHTMLElement4 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_BEFOREACTIVATE, &v);
}

static HRESULT WINAPI HTMLElement4_get_onbeforeactivate(IHTMLElement4 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_BEFOREACTIVATE, p);
}

static HRESULT WINAPI HTMLElement4_put_onfocusin(IHTMLElement4 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_FOCUSIN, &v);
}

static HRESULT WINAPI HTMLElement4_get_onfocusin(IHTMLElement4 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_FOCUSIN, p);
}

static HRESULT WINAPI HTMLElement4_put_onfocusout(IHTMLElement4 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_FOCUSOUT, &v);
}

static HRESULT WINAPI HTMLElement4_get_onfocusout(IHTMLElement4 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement4(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_FOCUSOUT, p);
}

static const IHTMLElement4Vtbl HTMLElement4Vtbl = {
    HTMLElement4_QueryInterface,
    HTMLElement4_AddRef,
    HTMLElement4_Release,
    HTMLElement4_GetTypeInfoCount,
    HTMLElement4_GetTypeInfo,
    HTMLElement4_GetIDsOfNames,
    HTMLElement4_Invoke,
    HTMLElement4_put_onmousewheel,
    HTMLElement4_get_onmousewheel,
    HTMLElement4_normalize,
    HTMLElement4_getAttributeNode,
    HTMLElement4_setAttributeNode,
    HTMLElement4_removeAttributeNode,
    HTMLElement4_put_onbeforeactivate,
    HTMLElement4_get_onbeforeactivate,
    HTMLElement4_put_onfocusin,
    HTMLElement4_get_onfocusin,
    HTMLElement4_put_onfocusout,
    HTMLElement4_get_onfocusout
};

static inline HTMLElement *impl_from_IHTMLElement6(IHTMLElement6 *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IHTMLElement6_iface);
}

static HRESULT WINAPI HTMLElement6_QueryInterface(IHTMLElement6 *iface,
        REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI HTMLElement6_AddRef(IHTMLElement6 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI HTMLElement6_Release(IHTMLElement6 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI HTMLElement6_GetTypeInfoCount(IHTMLElement6 *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLElement6_GetTypeInfo(IHTMLElement6 *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLElement6_GetIDsOfNames(IHTMLElement6 *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLElement6_Invoke(IHTMLElement6 *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLElement6_getAttributeNS(IHTMLElement6 *iface, VARIANT *pvarNS, BSTR strAttributeName, VARIANT *AttributeValue)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    nsAString ns_str, name_str, value_str;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s %s %p)\n", This, debugstr_variant(pvarNS), debugstr_w(strAttributeName), AttributeValue);

    if(!This->dom_element) {
        FIXME("No dom_element\n");
        return E_NOTIMPL;
    }

    hres = variant_to_nsstr(pvarNS, FALSE, &ns_str);
    if(FAILED(hres))
        return hres;

    nsAString_InitDepend(&name_str, strAttributeName);
    nsAString_InitDepend(&value_str, NULL);
    nsres = nsIDOMElement_GetAttributeNS(This->dom_element, &ns_str, &name_str, &value_str);
    nsAString_Finish(&ns_str);
    nsAString_Finish(&name_str);

    hres = return_nsstr_variant(nsres, &value_str, 0, AttributeValue);
    if(SUCCEEDED(hres) && V_VT(AttributeValue) == VT_NULL) {
        V_VT(AttributeValue) = VT_BSTR;
        V_BSTR(AttributeValue) = NULL;
    }
    return hres;
}

static HRESULT WINAPI HTMLElement6_setAttributeNS(IHTMLElement6 *iface, VARIANT *pvarNS, BSTR strAttributeName, VARIANT *pvarAttributeValue)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    nsAString ns_str, name_str, value_str;
    const PRUnichar *ns;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s %s %s)\n", This, debugstr_variant(pvarNS), debugstr_w(strAttributeName), debugstr_variant(pvarAttributeValue));

    hres = variant_to_nsstr(pvarNS, FALSE, &ns_str);
    if(FAILED(hres))
        return hres;
    nsAString_GetData(&ns_str, &ns);
    if((!ns || !ns[0]) && wcschr(strAttributeName, ':')) {
        nsAString_Finish(&ns_str);
        /* FIXME: Return NamespaceError */
        return E_FAIL;
    }

    if(!This->dom_element) {
        FIXME("No dom_element\n");
        nsAString_Finish(&ns_str);
        return E_NOTIMPL;
    }

    hres = variant_to_nsstr(pvarAttributeValue, FALSE, &value_str);
    if(FAILED(hres)) {
        nsAString_Finish(&ns_str);
        return hres;
    }

    nsAString_InitDepend(&name_str, strAttributeName);
    nsres = nsIDOMElement_SetAttributeNS(This->dom_element, &ns_str, &name_str, &value_str);
    nsAString_Finish(&ns_str);
    nsAString_Finish(&name_str);
    nsAString_Finish(&value_str);
    if(NS_FAILED(nsres))
        WARN("SetAttributeNS failed: %08lx\n", nsres);
    return map_nsresult(nsres);
}

static HRESULT WINAPI HTMLElement6_removeAttributeNS(IHTMLElement6 *iface, VARIANT *pvarNS, BSTR strAttributeName)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    nsAString ns_str, name_str;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s %s)\n", This, debugstr_variant(pvarNS), debugstr_w(strAttributeName));

    if(!This->dom_element) {
        FIXME("No dom_element\n");
        return E_NOTIMPL;
    }

    hres = variant_to_nsstr(pvarNS, FALSE, &ns_str);
    if(FAILED(hres))
        return hres;

    nsAString_InitDepend(&name_str, strAttributeName);
    nsres = nsIDOMElement_RemoveAttributeNS(This->dom_element, &ns_str, &name_str);
    nsAString_Finish(&ns_str);
    nsAString_Finish(&name_str);
    return map_nsresult(nsres);
}

static HRESULT WINAPI HTMLElement6_getAttributeNodeNS(IHTMLElement6 *iface, VARIANT *pvarNS, BSTR name, IHTMLDOMAttribute2 **ppretAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s %s %p)\n", This, debugstr_variant(pvarNS), debugstr_w(name), ppretAttribute);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_setAttributeNodeNS(IHTMLElement6 *iface, IHTMLDOMAttribute2 *pattr, IHTMLDOMAttribute2 **ppretAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p %p)\n", This, pattr, ppretAttribute);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_hasAttributeNS(IHTMLElement6 *iface, VARIANT *pvarNS, BSTR name, VARIANT_BOOL *pfHasAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    nsAString ns_str, name_str;
    nsresult nsres;
    HRESULT hres;
    cpp_bool r;

    TRACE("(%p)->(%s %s %p)\n", This, debugstr_variant(pvarNS), debugstr_w(name), pfHasAttribute);

    if(!This->dom_element) {
        FIXME("No dom_element\n");
        return E_NOTIMPL;
    }

    hres = variant_to_nsstr(pvarNS, FALSE, &ns_str);
    if(FAILED(hres))
        return hres;

    nsAString_InitDepend(&name_str, name);
    nsres = nsIDOMElement_HasAttributeNS(This->dom_element, &ns_str, &name_str, &r);
    nsAString_Finish(&ns_str);
    nsAString_Finish(&name_str);
    *pfHasAttribute = variant_bool(NS_SUCCEEDED(nsres) && r);
    return S_OK;
}

static HRESULT WINAPI HTMLElement6_getAttribute(IHTMLElement6 *iface, BSTR strAttributeName, VARIANT *AttributeValue)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    WARN("(%p)->(%s %p) forwarding to IHTMLElement\n", This, debugstr_w(strAttributeName), AttributeValue);

    return IHTMLElement_getAttribute(&This->IHTMLElement_iface, strAttributeName, 0, AttributeValue);
}

static HRESULT WINAPI HTMLElement6_setAttribute(IHTMLElement6 *iface, BSTR strAttributeName, VARIANT *pvarAttributeValue)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    WARN("(%p)->(%s %p) forwarding to IHTMLElement\n", This, debugstr_w(strAttributeName), pvarAttributeValue);

    return IHTMLElement_setAttribute(&This->IHTMLElement_iface, strAttributeName, *pvarAttributeValue, 0);
}

static HRESULT WINAPI HTMLElement6_removeAttribute(IHTMLElement6 *iface, BSTR strAttributeName)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    VARIANT_BOOL success;

    WARN("(%p)->(%s) forwarding to IHTMLElement\n", This, debugstr_w(strAttributeName));

    return IHTMLElement_removeAttribute(&This->IHTMLElement_iface, strAttributeName, 0, &success);
}

static HRESULT WINAPI HTMLElement6_getAttributeNode(IHTMLElement6 *iface, BSTR strAttributeName, IHTMLDOMAttribute2 **ppretAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    IHTMLDOMAttribute *attr;
    HRESULT hres;

    WARN("(%p)->(%s %p) forwarding to IHTMLElement4\n", This, debugstr_w(strAttributeName), ppretAttribute);

    hres = IHTMLElement4_getAttributeNode(&This->IHTMLElement4_iface, strAttributeName, &attr);
    if(FAILED(hres))
        return hres;

    if(attr) {
        hres = IHTMLDOMAttribute_QueryInterface(attr, &IID_IHTMLDOMAttribute2, (void**)ppretAttribute);
        IHTMLDOMAttribute_Release(attr);
    }else {
        *ppretAttribute = NULL;
    }
    return hres;
}

static HRESULT WINAPI HTMLElement6_setAttributeNode(IHTMLElement6 *iface, IHTMLDOMAttribute2 *pattr, IHTMLDOMAttribute2 **ppretAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    IHTMLDOMAttribute *attr, *ret_attr;
    HRESULT hres;

    WARN("(%p)->(%p %p) forwarding to IHTMLElement4\n", This, pattr, ppretAttribute);

    hres = IHTMLDOMAttribute2_QueryInterface(pattr, &IID_IHTMLDOMAttribute, (void**)&attr);
    if(FAILED(hres))
        return hres;

    hres = IHTMLElement4_setAttributeNode(&This->IHTMLElement4_iface, attr, &ret_attr);
    if(FAILED(hres))
        return hres;

    if(ret_attr) {
        hres = IHTMLDOMAttribute_QueryInterface(ret_attr, &IID_IHTMLDOMAttribute2, (void**)ppretAttribute);
        IHTMLDOMAttribute_Release(ret_attr);
    }else {
        *ppretAttribute = NULL;
    }
    return hres;
}

static HRESULT WINAPI HTMLElement6_removeAttributeNode(IHTMLElement6 *iface, IHTMLDOMAttribute2 *pattr, IHTMLDOMAttribute2 **ppretAttribute)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_hasAttribute(IHTMLElement6 *iface, BSTR name, VARIANT_BOOL *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(name), p);

    *p = element_has_attribute(This, name);
    return S_OK;
}

static HRESULT WINAPI HTMLElement6_getElementsByTagNameNS(IHTMLElement6 *iface, VARIANT *varNS, BSTR bstrLocalName, IHTMLElementCollection **pelColl)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s %s %p)\n", This, debugstr_variant(varNS), debugstr_w(bstrLocalName), pelColl);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_tagName(IHTMLElement6 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return IHTMLElement_get_tagName(&This->IHTMLElement_iface, p);
}

static HRESULT WINAPI HTMLElement6_get_nodeName(IHTMLElement6 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return IHTMLDOMNode_get_nodeName(&This->node.IHTMLDOMNode_iface, p);
}

static HRESULT WINAPI HTMLElement6_getElementsByClassName(IHTMLElement6 *iface, BSTR v, IHTMLElementCollection **pel)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    nsIDOMHTMLCollection *nscol = NULL;
    nsAString nsstr;
    nsresult nsres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(v), pel);

    if(This->dom_element) {
        nsAString_InitDepend(&nsstr, v);
        nsres = nsIDOMElement_GetElementsByClassName(This->dom_element, &nsstr, &nscol);
        nsAString_Finish(&nsstr);
        if(NS_FAILED(nsres)) {
            ERR("GetElementsByClassName failed: %08lx\n", nsres);
            return E_FAIL;
        }
    }

    *pel = create_collection_from_htmlcol(nscol, This->node.doc, dispex_compat_mode(&This->node.event_target.dispex));
    nsIDOMHTMLCollection_Release(nscol);
    return S_OK;
}

static HRESULT WINAPI HTMLElement6_msMatchesSelector(IHTMLElement6 *iface, BSTR v, VARIANT_BOOL *pfMatches)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    nsAString nsstr;
    cpp_bool b;
    nsresult nsres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(v), pfMatches);

    if(!This->dom_element) {
        FIXME("No dom element\n");
        return E_UNEXPECTED;
    }

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMElement_MozMatchesSelector(This->dom_element, &nsstr, &b);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres)) {
        WARN("MozMatchesSelector failed: %08lx\n", nsres);
        return map_nsresult(nsres);
    }

    *pfMatches = b;
    return S_OK;
}

static HRESULT WINAPI HTMLElement6_put_onabort(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_ABORT, &v);
}

static HRESULT WINAPI HTMLElement6_get_onabort(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_ABORT, p);
}

static HRESULT WINAPI HTMLElement6_put_oncanplay(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_oncanplay(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_oncanplaythrough(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_oncanplaythrough(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onchange(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_CHANGE, &v);
}

static HRESULT WINAPI HTMLElement6_get_onchange(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_CHANGE, p);
}

static HRESULT WINAPI HTMLElement6_put_ondurationchange(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_ondurationchange(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onemptied(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onemptied(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onended(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onended(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onerror(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_ERROR, &v);
}

static HRESULT WINAPI HTMLElement6_get_onerror(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_ERROR, p);
}

static HRESULT WINAPI HTMLElement6_put_oninput(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_INPUT, &v);
}

static HRESULT WINAPI HTMLElement6_get_oninput(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_INPUT, p);
}

static HRESULT WINAPI HTMLElement6_put_onload(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_LOAD, &v);
}

static HRESULT WINAPI HTMLElement6_get_onload(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_LOAD, p);
}

static HRESULT WINAPI HTMLElement6_put_onloadeddata(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onloadeddata(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onloadedmetadata(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onloadedmetadata(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onloadstart(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onloadstart(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onpause(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onpause(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onplay(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onplay(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onplaying(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onplaying(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onprogress(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onprogress(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onratechange(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onratechange(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onreset(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onreset(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onseeked(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onseeked(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onseeking(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onseeking(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onselect(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onselect(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onstalled(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onstalled(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onsubmit(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    return set_node_event(&This->node, EVENTID_SUBMIT, &v);
}

static HRESULT WINAPI HTMLElement6_get_onsubmit(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);

    TRACE("(%p)->(%p)\n", This, p);

    return get_node_event(&This->node, EVENTID_SUBMIT, p);
}

static HRESULT WINAPI HTMLElement6_put_onsuspend(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onsuspend(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_ontimeupdate(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_ontimeupdate(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onvolumechange(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onvolumechange(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_put_onwaiting(IHTMLElement6 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_get_onwaiting(IHTMLElement6 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement6_hasAttributes(IHTMLElement6 *iface, VARIANT_BOOL *pfHasAttributes)
{
    HTMLElement *This = impl_from_IHTMLElement6(iface);
    FIXME("(%p)->(%p)\n", This, pfHasAttributes);
    return E_NOTIMPL;
}

static const IHTMLElement6Vtbl HTMLElement6Vtbl = {
    HTMLElement6_QueryInterface,
    HTMLElement6_AddRef,
    HTMLElement6_Release,
    HTMLElement6_GetTypeInfoCount,
    HTMLElement6_GetTypeInfo,
    HTMLElement6_GetIDsOfNames,
    HTMLElement6_Invoke,
    HTMLElement6_getAttributeNS,
    HTMLElement6_setAttributeNS,
    HTMLElement6_removeAttributeNS,
    HTMLElement6_getAttributeNodeNS,
    HTMLElement6_setAttributeNodeNS,
    HTMLElement6_hasAttributeNS,
    HTMLElement6_getAttribute,
    HTMLElement6_setAttribute,
    HTMLElement6_removeAttribute,
    HTMLElement6_getAttributeNode,
    HTMLElement6_setAttributeNode,
    HTMLElement6_removeAttributeNode,
    HTMLElement6_hasAttribute,
    HTMLElement6_getElementsByTagNameNS,
    HTMLElement6_get_tagName,
    HTMLElement6_get_nodeName,
    HTMLElement6_getElementsByClassName,
    HTMLElement6_msMatchesSelector,
    HTMLElement6_put_onabort,
    HTMLElement6_get_onabort,
    HTMLElement6_put_oncanplay,
    HTMLElement6_get_oncanplay,
    HTMLElement6_put_oncanplaythrough,
    HTMLElement6_get_oncanplaythrough,
    HTMLElement6_put_onchange,
    HTMLElement6_get_onchange,
    HTMLElement6_put_ondurationchange,
    HTMLElement6_get_ondurationchange,
    HTMLElement6_put_onemptied,
    HTMLElement6_get_onemptied,
    HTMLElement6_put_onended,
    HTMLElement6_get_onended,
    HTMLElement6_put_onerror,
    HTMLElement6_get_onerror,
    HTMLElement6_put_oninput,
    HTMLElement6_get_oninput,
    HTMLElement6_put_onload,
    HTMLElement6_get_onload,
    HTMLElement6_put_onloadeddata,
    HTMLElement6_get_onloadeddata,
    HTMLElement6_put_onloadedmetadata,
    HTMLElement6_get_onloadedmetadata,
    HTMLElement6_put_onloadstart,
    HTMLElement6_get_onloadstart,
    HTMLElement6_put_onpause,
    HTMLElement6_get_onpause,
    HTMLElement6_put_onplay,
    HTMLElement6_get_onplay,
    HTMLElement6_put_onplaying,
    HTMLElement6_get_onplaying,
    HTMLElement6_put_onprogress,
    HTMLElement6_get_onprogress,
    HTMLElement6_put_onratechange,
    HTMLElement6_get_onratechange,
    HTMLElement6_put_onreset,
    HTMLElement6_get_onreset,
    HTMLElement6_put_onseeked,
    HTMLElement6_get_onseeked,
    HTMLElement6_put_onseeking,
    HTMLElement6_get_onseeking,
    HTMLElement6_put_onselect,
    HTMLElement6_get_onselect,
    HTMLElement6_put_onstalled,
    HTMLElement6_get_onstalled,
    HTMLElement6_put_onsubmit,
    HTMLElement6_get_onsubmit,
    HTMLElement6_put_onsuspend,
    HTMLElement6_get_onsuspend,
    HTMLElement6_put_ontimeupdate,
    HTMLElement6_get_ontimeupdate,
    HTMLElement6_put_onvolumechange,
    HTMLElement6_get_onvolumechange,
    HTMLElement6_put_onwaiting,
    HTMLElement6_get_onwaiting,
    HTMLElement6_hasAttributes
};

static inline HTMLElement *impl_from_IHTMLElement7(IHTMLElement7 *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IHTMLElement7_iface);
}

static HRESULT WINAPI HTMLElement7_QueryInterface(IHTMLElement7 *iface,
        REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI HTMLElement7_AddRef(IHTMLElement7 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI HTMLElement7_Release(IHTMLElement7 *iface)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI HTMLElement7_GetTypeInfoCount(IHTMLElement7 *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLElement7_GetTypeInfo(IHTMLElement7 *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLElement7_GetIDsOfNames(IHTMLElement7 *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLElement7_Invoke(IHTMLElement7 *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLElement7_put_onmspointerdown(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmspointerdown(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmspointermove(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmspointermove(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmspointerup(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmspointerup(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmspointerover(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmspointerover(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmspointerout(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmspointerout(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmspointercancel(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmspointercancel(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmspointerhover(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmspointerhover(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmslostpointercapture(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmslostpointercapture(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsgotpointercapture(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsgotpointercapture(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsgesturestart(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsgesturestart(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsgesturechange(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsgesturechange(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsgestureend(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsgestureend(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsgesturehold(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsgesturehold(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsgesturetap(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsgesturetap(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsgesturedoubletap(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsgesturedoubletap(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsinertiastart(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsinertiastart(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_msSetPointerCapture(IHTMLElement7 *iface, LONG pointer_id)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%ld)\n", This, pointer_id);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_msReleasePointerCapture(IHTMLElement7 *iface, LONG pointer_id)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%ld)\n", This, pointer_id);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmstransitionstart(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmstransitionstart(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmstransitionend(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmstransitionend(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsanimationstart(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsanimationstart(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsanimationend(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsanimationend(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_onmsanimationiteration(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsanimationiteration(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_oninvalid(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_oninvalid(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_xmsAcceleratorKey(IHTMLElement7 *iface, BSTR v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_w(v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_xmsAcceleratorKey(IHTMLElement7 *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_spellcheck(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);

    TRACE("(%p)->(%s)\n", This, debugstr_variant(&v));

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    if(V_VT(&v) != VT_BOOL) {
        FIXME("unsupported argument %s\n", debugstr_variant(&v));
        return E_NOTIMPL;
    }

    return map_nsresult(nsIDOMHTMLElement_SetSpellcheck(This->html_element, !!V_BOOL(&v)));
}

static HRESULT WINAPI HTMLElement7_get_spellcheck(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    cpp_bool spellcheck;
    nsresult nsres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->html_element) {
        FIXME("non-HTML element\n");
        return E_NOTIMPL;
    }

    nsres = nsIDOMHTMLElement_GetSpellcheck(This->html_element, &spellcheck);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);

    V_VT(p) = VT_BOOL;
    V_BOOL(p) = spellcheck ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI HTMLElement7_put_onmsmanipulationstatechanged(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_onmsmanipulationstatechanged(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_put_oncuechange(IHTMLElement7 *iface, VARIANT v)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%s)\n", This, debugstr_variant(&v));
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLElement7_get_oncuechange(IHTMLElement7 *iface, VARIANT *p)
{
    HTMLElement *This = impl_from_IHTMLElement7(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static const IHTMLElement7Vtbl HTMLElement7Vtbl = {
    HTMLElement7_QueryInterface,
    HTMLElement7_AddRef,
    HTMLElement7_Release,
    HTMLElement7_GetTypeInfoCount,
    HTMLElement7_GetTypeInfo,
    HTMLElement7_GetIDsOfNames,
    HTMLElement7_Invoke,
    HTMLElement7_put_onmspointerdown,
    HTMLElement7_get_onmspointerdown,
    HTMLElement7_put_onmspointermove,
    HTMLElement7_get_onmspointermove,
    HTMLElement7_put_onmspointerup,
    HTMLElement7_get_onmspointerup,
    HTMLElement7_put_onmspointerover,
    HTMLElement7_get_onmspointerover,
    HTMLElement7_put_onmspointerout,
    HTMLElement7_get_onmspointerout,
    HTMLElement7_put_onmspointercancel,
    HTMLElement7_get_onmspointercancel,
    HTMLElement7_put_onmspointerhover,
    HTMLElement7_get_onmspointerhover,
    HTMLElement7_put_onmslostpointercapture,
    HTMLElement7_get_onmslostpointercapture,
    HTMLElement7_put_onmsgotpointercapture,
    HTMLElement7_get_onmsgotpointercapture,
    HTMLElement7_put_onmsgesturestart,
    HTMLElement7_get_onmsgesturestart,
    HTMLElement7_put_onmsgesturechange,
    HTMLElement7_get_onmsgesturechange,
    HTMLElement7_put_onmsgestureend,
    HTMLElement7_get_onmsgestureend,
    HTMLElement7_put_onmsgesturehold,
    HTMLElement7_get_onmsgesturehold,
    HTMLElement7_put_onmsgesturetap,
    HTMLElement7_get_onmsgesturetap,
    HTMLElement7_put_onmsgesturedoubletap,
    HTMLElement7_get_onmsgesturedoubletap,
    HTMLElement7_put_onmsinertiastart,
    HTMLElement7_get_onmsinertiastart,
    HTMLElement7_msSetPointerCapture,
    HTMLElement7_msReleasePointerCapture,
    HTMLElement7_put_onmstransitionstart,
    HTMLElement7_get_onmstransitionstart,
    HTMLElement7_put_onmstransitionend,
    HTMLElement7_get_onmstransitionend,
    HTMLElement7_put_onmsanimationstart,
    HTMLElement7_get_onmsanimationstart,
    HTMLElement7_put_onmsanimationend,
    HTMLElement7_get_onmsanimationend,
    HTMLElement7_put_onmsanimationiteration,
    HTMLElement7_get_onmsanimationiteration,
    HTMLElement7_put_oninvalid,
    HTMLElement7_get_oninvalid,
    HTMLElement7_put_xmsAcceleratorKey,
    HTMLElement7_get_xmsAcceleratorKey,
    HTMLElement7_put_spellcheck,
    HTMLElement7_get_spellcheck,
    HTMLElement7_put_onmsmanipulationstatechanged,
    HTMLElement7_get_onmsmanipulationstatechanged,
    HTMLElement7_put_oncuechange,
    HTMLElement7_get_oncuechange
};


static inline HTMLElement *impl_from_IHTMLUniqueName(IHTMLUniqueName *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IHTMLUniqueName_iface);
}

static HRESULT WINAPI HTMLUniqueName_QueryInterface(IHTMLUniqueName *iface, REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI HTMLUniqueName_AddRef(IHTMLUniqueName *iface)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI HTMLUniqueName_Release(IHTMLUniqueName *iface)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI HTMLUniqueName_GetTypeInfoCount(IHTMLUniqueName *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLUniqueName_GetTypeInfo(IHTMLUniqueName *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLUniqueName_GetIDsOfNames(IHTMLUniqueName *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLUniqueName_Invoke(IHTMLUniqueName *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

HRESULT elem_unique_id(unsigned id, BSTR *p)
{
    WCHAR buf[32];

    swprintf(buf, ARRAY_SIZE(buf), L"ms__id%u", id);
    *p = SysAllocString(buf);
    return *p ? S_OK : E_OUTOFMEMORY;
}

static void ensure_unique_id(HTMLElement *elem)
{
    if(!elem->unique_id)
        elem->unique_id = ++elem->node.doc->unique_id;
}

static HRESULT WINAPI HTMLUniqueName_get_uniqueNumber(IHTMLUniqueName *iface, LONG *p)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);

    TRACE("(%p)->(%p)\n", This, p);

    ensure_unique_id(This);
    *p = This->unique_id;
    return S_OK;
}

static HRESULT WINAPI HTMLUniqueName_get_uniqueID(IHTMLUniqueName *iface, BSTR *p)
{
    HTMLElement *This = impl_from_IHTMLUniqueName(iface);

    TRACE("(%p)->(%p)\n", This, p);

    ensure_unique_id(This);
    return elem_unique_id(This->unique_id, p);
}

static const IHTMLUniqueNameVtbl HTMLUniqueNameVtbl = {
    HTMLUniqueName_QueryInterface,
    HTMLUniqueName_AddRef,
    HTMLUniqueName_Release,
    HTMLUniqueName_GetTypeInfoCount,
    HTMLUniqueName_GetTypeInfo,
    HTMLUniqueName_GetIDsOfNames,
    HTMLUniqueName_Invoke,
    HTMLUniqueName_get_uniqueNumber,
    HTMLUniqueName_get_uniqueID
};

static inline HTMLElement *impl_from_IElementSelector(IElementSelector *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IElementSelector_iface);
}

static HRESULT WINAPI ElementSelector_QueryInterface(IElementSelector *iface, REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI ElementSelector_AddRef(IElementSelector *iface)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI ElementSelector_Release(IElementSelector *iface)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI ElementSelector_GetTypeInfoCount(IElementSelector *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI ElementSelector_GetTypeInfo(IElementSelector *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI ElementSelector_GetIDsOfNames(IElementSelector *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames, lcid, rgDispId);
}

static HRESULT WINAPI ElementSelector_Invoke(IElementSelector *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid, wFlags,
            pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI ElementSelector_querySelector(IElementSelector *iface, BSTR v, IHTMLElement **pel)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    nsIDOMElement *nselem;
    HTMLElement *elem;
    nsAString nsstr;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(v), pel);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMElement_QuerySelector(This->dom_element, &nsstr, &nselem);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres)) {
        WARN("QuerySelector failed: %08lx\n", nsres);
        return map_nsresult(nsres);
    }

    if(!nselem) {
        *pel = NULL;
        return S_OK;
    }

    hres = get_element(nselem, &elem);
    nsIDOMElement_Release(nselem);
    if(FAILED(hres))
        return hres;

    *pel = &elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI ElementSelector_querySelectorAll(IElementSelector *iface, BSTR v, IHTMLDOMChildrenCollection **pel)
{
    HTMLElement *This = impl_from_IElementSelector(iface);
    nsIDOMNodeList *node_list;
    nsAString nsstr;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(v), pel);

    if(!This->dom_element) {
        FIXME("comment element\n");
        return E_NOTIMPL;
    }

    nsAString_InitDepend(&nsstr, v);
    nsres = nsIDOMElement_QuerySelectorAll(This->dom_element, &nsstr, &node_list);
    nsAString_Finish(&nsstr);
    if(NS_FAILED(nsres)) {
        WARN("QuerySelectorAll failed: %08lx\n", nsres);
        return map_nsresult(nsres);
    }

    hres = create_child_collection(node_list, This->node.doc, pel);
    nsIDOMNodeList_Release(node_list);
    return hres;
}

static const IElementSelectorVtbl ElementSelectorVtbl = {
    ElementSelector_QueryInterface,
    ElementSelector_AddRef,
    ElementSelector_Release,
    ElementSelector_GetTypeInfoCount,
    ElementSelector_GetTypeInfo,
    ElementSelector_GetIDsOfNames,
    ElementSelector_Invoke,
    ElementSelector_querySelector,
    ElementSelector_querySelectorAll
};

static inline HTMLElement *impl_from_IProvideMultipleClassInfo(IProvideMultipleClassInfo *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IProvideMultipleClassInfo_iface);
}

static HRESULT WINAPI ProvideClassInfo_QueryInterface(IProvideMultipleClassInfo *iface,
        REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IProvideMultipleClassInfo(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI ProvideClassInfo_AddRef(IProvideMultipleClassInfo *iface)
{
    HTMLElement *This = impl_from_IProvideMultipleClassInfo(iface);
    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI ProvideClassInfo_Release(IProvideMultipleClassInfo *iface)
{
    HTMLElement *This = impl_from_IProvideMultipleClassInfo(iface);
    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI ProvideClassInfo_GetClassInfo(IProvideMultipleClassInfo *iface, ITypeInfo **ppTI)
{
    HTMLElement *This = impl_from_IProvideMultipleClassInfo(iface);
    TRACE("(%p)->(%p)\n", This, ppTI);
    return get_class_typeinfo(This->node.vtbl->clsid, ppTI);
}

static HRESULT WINAPI ProvideClassInfo2_GetGUID(IProvideMultipleClassInfo *iface, DWORD dwGuidKind, GUID *pGUID)
{
    HTMLElement *This = impl_from_IProvideMultipleClassInfo(iface);
    FIXME("(%p)->(%lu %p)\n", This, dwGuidKind, pGUID);
    return E_NOTIMPL;
}

static HRESULT WINAPI ProvideMultipleClassInfo_GetMultiTypeInfoCount(IProvideMultipleClassInfo *iface, ULONG *pcti)
{
    HTMLElement *This = impl_from_IProvideMultipleClassInfo(iface);
    FIXME("(%p)->(%p)\n", This, pcti);
    *pcti = 1;
    return S_OK;
}

static HRESULT WINAPI ProvideMultipleClassInfo_GetInfoOfIndex(IProvideMultipleClassInfo *iface, ULONG iti,
        DWORD dwFlags, ITypeInfo **pptiCoClass, DWORD *pdwTIFlags, ULONG *pcdispidReserved, IID *piidPrimary, IID *piidSource)
{
    HTMLElement *This = impl_from_IProvideMultipleClassInfo(iface);
    FIXME("(%p)->(%lu %lx %p %p %p %p %p)\n", This, iti, dwFlags, pptiCoClass, pdwTIFlags, pcdispidReserved, piidPrimary, piidSource);
    return E_NOTIMPL;
}

static const IProvideMultipleClassInfoVtbl ProvideMultipleClassInfoVtbl = {
    ProvideClassInfo_QueryInterface,
    ProvideClassInfo_AddRef,
    ProvideClassInfo_Release,
    ProvideClassInfo_GetClassInfo,
    ProvideClassInfo2_GetGUID,
    ProvideMultipleClassInfo_GetMultiTypeInfoCount,
    ProvideMultipleClassInfo_GetInfoOfIndex
};

static inline HTMLElement *impl_from_IElementTraversal(IElementTraversal *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IElementTraversal_iface);
}

static HRESULT WINAPI ElementTraversal_QueryInterface(IElementTraversal *iface, REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI ElementTraversal_AddRef(IElementTraversal *iface)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);

    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI ElementTraversal_Release(IElementTraversal *iface)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);

    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI ElementTraversal_GetTypeInfoCount(IElementTraversal *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    return IDispatchEx_GetTypeInfoCount(&This->node.event_target.dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI ElementTraversal_GetTypeInfo(IElementTraversal *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    return IDispatchEx_GetTypeInfo(&This->node.event_target.dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI ElementTraversal_GetIDsOfNames(IElementTraversal *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    return IDispatchEx_GetIDsOfNames(&This->node.event_target.dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI ElementTraversal_Invoke(IElementTraversal *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    return IDispatchEx_Invoke(&This->node.event_target.dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI ElementTraversal_get_firstElementChild(IElementTraversal *iface, IHTMLElement **p)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    nsIDOMElement *nselem = NULL;
    HTMLElement *elem;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        *p = NULL;
        return S_OK;
    }

    nsIDOMElement_GetFirstElementChild(This->dom_element, &nselem);
    if(!nselem) {
        *p = NULL;
        return S_OK;
    }

    hres = get_element(nselem, &elem);
    nsIDOMElement_Release(nselem);
    if(FAILED(hres))
        return hres;

    *p = &elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI ElementTraversal_get_lastElementChild(IElementTraversal *iface, IHTMLElement **p)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    nsIDOMElement *nselem = NULL;
    HTMLElement *elem;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        *p = NULL;
        return S_OK;
    }

    nsIDOMElement_GetLastElementChild(This->dom_element, &nselem);
    if(!nselem) {
        *p = NULL;
        return S_OK;
    }

    hres = get_element(nselem, &elem);
    nsIDOMElement_Release(nselem);
    if(FAILED(hres))
        return hres;

    *p = &elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI ElementTraversal_get_previousElementSibling(IElementTraversal *iface, IHTMLElement **p)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    nsIDOMElement *nselem = NULL;
    HTMLElement *elem;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        *p = NULL;
        return S_OK;
    }

    nsIDOMElement_GetPreviousElementSibling(This->dom_element, &nselem);
    if(!nselem) {
        *p = NULL;
        return S_OK;
    }

    hres = get_element(nselem, &elem);
    nsIDOMElement_Release(nselem);
    if(FAILED(hres))
        return hres;

    *p = &elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI ElementTraversal_get_nextElementSibling(IElementTraversal *iface, IHTMLElement **p)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    nsIDOMElement *nselem = NULL;
    HTMLElement *elem;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    if(!This->dom_element) {
        *p = NULL;
        return S_OK;
    }

    nsIDOMElement_GetNextElementSibling(This->dom_element, &nselem);
    if(!nselem) {
        *p = NULL;
        return S_OK;
    }

    hres = get_element(nselem, &elem);
    nsIDOMElement_Release(nselem);
    if(FAILED(hres))
        return hres;

    *p = &elem->IHTMLElement_iface;
    return S_OK;
}

static HRESULT WINAPI ElementTraversal_get_childElementCount(IElementTraversal *iface, LONG *p)
{
    HTMLElement *This = impl_from_IElementTraversal(iface);
    UINT32 count = 0;

    TRACE("(%p)->(%p)\n", This, p);

    if(This->dom_element)
        nsIDOMElement_GetChildElementCount(This->dom_element, &count);

    *p = count;
    return S_OK;
}

static const IElementTraversalVtbl ElementTraversalVtbl = {
    ElementTraversal_QueryInterface,
    ElementTraversal_AddRef,
    ElementTraversal_Release,
    ElementTraversal_GetTypeInfoCount,
    ElementTraversal_GetTypeInfo,
    ElementTraversal_GetIDsOfNames,
    ElementTraversal_Invoke,
    ElementTraversal_get_firstElementChild,
    ElementTraversal_get_lastElementChild,
    ElementTraversal_get_previousElementSibling,
    ElementTraversal_get_nextElementSibling,
    ElementTraversal_get_childElementCount
};

static inline HTMLElement *impl_from_HTMLDOMNode(HTMLDOMNode *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, node);
}

HRESULT HTMLElement_QI(HTMLDOMNode *iface, REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_HTMLDOMNode(iface);

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        *ppv = &This->IHTMLElement_iface;
    }else if(IsEqualGUID(&IID_IDispatch, riid)) {
        *ppv = &This->IHTMLElement_iface;
    }else if(IsEqualGUID(&IID_IHTMLElement, riid)) {
        *ppv = &This->IHTMLElement_iface;
    }else if(IsEqualGUID(&IID_IHTMLElement2, riid)) {
        *ppv = &This->IHTMLElement2_iface;
    }else if(IsEqualGUID(&IID_IHTMLElement3, riid)) {
        *ppv = &This->IHTMLElement3_iface;
    }else if(IsEqualGUID(&IID_IHTMLElement4, riid)) {
        *ppv = &This->IHTMLElement4_iface;
    }else if(IsEqualGUID(&IID_IHTMLElement6, riid)) {
        *ppv = &This->IHTMLElement6_iface;
    }else if(IsEqualGUID(&IID_IHTMLElement7, riid)) {
        *ppv = &This->IHTMLElement7_iface;
    }else if(IsEqualGUID(&IID_IHTMLUniqueName, riid)) {
        *ppv = &This->IHTMLUniqueName_iface;
    }else if(IsEqualGUID(&IID_IElementSelector, riid)) {
        *ppv = &This->IElementSelector_iface;
    }else if(IsEqualGUID(&IID_IElementTraversal, riid)) {
        *ppv = &This->IElementTraversal_iface;
    }else if(IsEqualGUID(&IID_IConnectionPointContainer, riid)) {
        *ppv = &This->cp_container.IConnectionPointContainer_iface;
    }else if(IsEqualGUID(&IID_IProvideClassInfo, riid)) {
        *ppv = &This->IProvideMultipleClassInfo_iface;
    }else if(IsEqualGUID(&IID_IProvideClassInfo2, riid)) {
        *ppv = &This->IProvideMultipleClassInfo_iface;
    }else if(IsEqualGUID(&IID_IProvideMultipleClassInfo, riid)) {
        *ppv = &This->IProvideMultipleClassInfo_iface;
    }else if(IsEqualGUID(&IID_IWineHTMLElementPrivate, riid)) {
        *ppv = &This->IWineHTMLElementPrivate_iface;
    }else {
        return HTMLDOMNode_QI(&This->node, riid, ppv);
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

void HTMLElement_destructor(HTMLDOMNode *iface)
{
    HTMLElement *This = impl_from_HTMLDOMNode(iface);

    ConnectionPointContainer_Destroy(&This->cp_container);

    if(This->style) {
        This->style->elem = NULL;
        IHTMLStyle_Release(&This->style->IHTMLStyle_iface);
    }
    if(This->runtime_style) {
        This->runtime_style->elem = NULL;
        IHTMLStyle_Release(&This->runtime_style->IHTMLStyle_iface);
    }
    if(This->attrs) {
        HTMLDOMAttribute *attr;

        LIST_FOR_EACH_ENTRY(attr, &This->attrs->attrs, HTMLDOMAttribute, entry)
            attr->elem = NULL;

        This->attrs->elem = NULL;
        IHTMLAttributeCollection_Release(&This->attrs->IHTMLAttributeCollection_iface);
    }

    free(This->filter);

    HTMLDOMNode_destructor(&This->node);
}

HRESULT HTMLElement_clone(HTMLDOMNode *iface, nsIDOMNode *nsnode, HTMLDOMNode **ret)
{
    HTMLElement *This = impl_from_HTMLDOMNode(iface);
    HTMLElement *new_elem;
    HRESULT hres;

    hres = HTMLElement_Create(This->node.doc, nsnode, FALSE, &new_elem);
    if(FAILED(hres))
        return hres;

    if(This->filter) {
        new_elem->filter = wcsdup(This->filter);
        if(!new_elem->filter) {
            IHTMLElement_Release(&This->IHTMLElement_iface);
            return E_OUTOFMEMORY;
        }
    }

    *ret = &new_elem->node;
    return S_OK;
}

HRESULT HTMLElement_dispatch_nsevent_hook(HTMLDOMNode *iface, DOMEvent *event)
{
    return S_FALSE;
}

HRESULT HTMLElement_handle_event(HTMLDOMNode *iface, DWORD eid, nsIDOMEvent *event, BOOL *prevent_default)
{
    HTMLElement *This = impl_from_HTMLDOMNode(iface);

    switch(eid) {
    case EVENTID_KEYDOWN: {
        nsIDOMKeyEvent *key_event;
        nsresult nsres;

        nsres = nsIDOMEvent_QueryInterface(event, &IID_nsIDOMKeyEvent, (void**)&key_event);
        if(NS_SUCCEEDED(nsres)) {
            UINT32 code = 0;

            nsIDOMKeyEvent_GetKeyCode(key_event, &code);

            if(code == VK_F1 /* DOM_VK_F1 */) {
                DOMEvent *help_event;
                HRESULT hres;

                TRACE("F1 pressed\n");

                hres = create_document_event(This->node.doc, EVENTID_HELP, &help_event);
                if(SUCCEEDED(hres)) {
                    dispatch_event(&This->node.event_target, help_event);
                    IDOMEvent_Release(&help_event->IDOMEvent_iface);
                }
                *prevent_default = TRUE;
            }

            nsIDOMKeyEvent_Release(key_event);
        }
    }
    }

    return S_OK;
}

cp_static_data_t HTMLElementEvents2_data = { HTMLElementEvents2_tid, NULL /* FIXME */, TRUE };

const cpc_entry_t HTMLElement_cpc[] = {
    HTMLELEMENT_CPC,
    {NULL}
};

static const NodeImplVtbl HTMLElementImplVtbl = {
    &CLSID_HTMLUnknownElement,
    HTMLElement_QI,
    HTMLElement_destructor,
    HTMLElement_cpc,
    HTMLElement_clone,
    HTMLElement_dispatch_nsevent_hook,
    HTMLElement_handle_event,
    HTMLElement_get_attr_col
};

static inline HTMLElement *impl_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, node.event_target.dispex);
}

static HRESULT HTMLElement_get_dispid(DispatchEx *dispex, BSTR name,
        DWORD grfdex, DISPID *pid)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);

    if(This->node.vtbl->get_dispid)
        return This->node.vtbl->get_dispid(&This->node, name, grfdex, pid);

    return DISP_E_UNKNOWNNAME;
}

static HRESULT HTMLElement_get_name(DispatchEx *dispex, DISPID id, BSTR *name)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);

    if(This->node.vtbl->get_name)
        return This->node.vtbl->get_name(&This->node, id, name);

    ERR("(%p): element has no get_name method\n", This);
    return DISP_E_MEMBERNOTFOUND;
}

static HRESULT HTMLElement_invoke(DispatchEx *dispex, IDispatch *this_obj, DISPID id, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);

    if(This->node.vtbl->invoke)
        return This->node.vtbl->invoke(&This->node, this_obj, id, lcid, flags,
                params, res, ei, caller);

    ERR("(%p): element has no invoke method\n", This);
    return E_NOTIMPL;
}

static HRESULT HTMLElement_populate_props(DispatchEx *dispex)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);
    nsIDOMMozNamedAttrMap *attrs;
    nsIDOMAttr *attr;
    nsAString nsstr;
    const PRUnichar *str;
    BSTR name;
    VARIANT value;
    unsigned i;
    UINT32 len;
    DISPID id;
    nsresult nsres;
    HRESULT hres;

    if(!This->dom_element)
        return S_FALSE;

    if(dispex_compat_mode(dispex) >= COMPAT_MODE_IE9)
        return S_OK;

    nsres = nsIDOMElement_GetAttributes(This->dom_element, &attrs);
    if(NS_FAILED(nsres))
        return E_FAIL;

    nsres = nsIDOMMozNamedAttrMap_GetLength(attrs, &len);
    if(NS_FAILED(nsres)) {
        nsIDOMMozNamedAttrMap_Release(attrs);
        return E_FAIL;
    }

    nsAString_Init(&nsstr, NULL);
    for(i=0; i<len; i++) {
        nsres = nsIDOMMozNamedAttrMap_Item(attrs, i, &attr);
        if(NS_FAILED(nsres))
            continue;

        nsres = nsIDOMAttr_GetNodeName(attr, &nsstr);
        if(NS_FAILED(nsres)) {
            nsIDOMAttr_Release(attr);
            continue;
        }

        nsAString_GetData(&nsstr, &str);
        name = SysAllocString(str);
        if(!name) {
            nsIDOMAttr_Release(attr);
            continue;
        }

        hres = IDispatchEx_GetDispID(&dispex->IDispatchEx_iface, name, fdexNameCaseInsensitive, &id);
        if(hres != DISP_E_UNKNOWNNAME) {
            nsIDOMAttr_Release(attr);
            SysFreeString(name);
            continue;
        }

        nsres = nsIDOMAttr_GetNodeValue(attr, &nsstr);
        nsIDOMAttr_Release(attr);
        if(NS_FAILED(nsres)) {
            SysFreeString(name);
            continue;
        }

        nsAString_GetData(&nsstr, &str);
        V_VT(&value) = VT_BSTR;
        if(*str) {
            V_BSTR(&value) = SysAllocString(str);
            if(!V_BSTR(&value)) {
                SysFreeString(name);
                continue;
            }
        } else
            V_BSTR(&value) = NULL;

        IHTMLElement_setAttribute(&This->IHTMLElement_iface, name, value, 0);
        SysFreeString(name);
        VariantClear(&value);
    }
    nsAString_Finish(&nsstr);

    nsIDOMMozNamedAttrMap_Release(attrs);
    return S_OK;
}

static nsISupports *HTMLElement_get_gecko_target(DispatchEx *dispex)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);
    return (nsISupports*)This->node.nsnode;
}

static void HTMLElement_bind_event(DispatchEx *dispex, eventid_t eid)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);
    ensure_doc_nsevent_handler(This->node.doc, This->node.nsnode, eid);
}

static HRESULT HTMLElement_event_target_dispatch_nsevent_hook(DispatchEx *dispex, DOMEvent *event)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);
    return This->node.vtbl->dispatch_nsevent_hook(&This->node, event);
}

static HRESULT HTMLElement_handle_event_default(DispatchEx *dispex, eventid_t eid, nsIDOMEvent *nsevent, BOOL *prevent_default)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);

    if(!This->node.vtbl->handle_event)
        return S_OK;
    return This->node.vtbl->handle_event(&This->node, eid, nsevent, prevent_default);
}

static EventTarget *HTMLElement_get_parent_event_target(DispatchEx *dispex)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);
    HTMLDOMNode *node;
    nsIDOMNode *nsnode;
    nsresult nsres;
    HRESULT hres;

    nsres = nsIDOMNode_GetParentNode(This->node.nsnode, &nsnode);
    assert(nsres == NS_OK);
    if(!nsnode)
        return NULL;

    hres = get_node(nsnode, TRUE, &node);
    nsIDOMNode_Release(nsnode);
    if(FAILED(hres))
        return NULL;

    return &node->event_target;
}

static ConnectionPointContainer *HTMLElement_get_cp_container(DispatchEx *dispex)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);
    IConnectionPointContainer_AddRef(&This->cp_container.IConnectionPointContainer_iface);
    return &This->cp_container;
}

static IHTMLEventObj *HTMLElement_set_current_event(DispatchEx *dispex, IHTMLEventObj *event)
{
    HTMLElement *This = impl_from_DispatchEx(dispex);
    return default_set_current_event(This->node.doc->window, event);
}

static HRESULT IHTMLElement6_hasAttributeNS_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp,
        VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    VARIANT args[2];
    HRESULT hres;
    DISPPARAMS new_dp = { args, NULL, 2, 0 };

    if(!(flags & DISPATCH_METHOD) || dp->cArgs < 2 || dp->cNamedArgs)
        return S_FALSE;

    switch(V_VT(&dp->rgvarg[dp->cArgs - 1])) {
    case VT_EMPTY:
    case VT_BSTR:
    case VT_NULL:
        return S_FALSE;
    default:
        break;
    }

    hres = change_type(&args[1], &dp->rgvarg[dp->cArgs - 1], VT_BSTR, caller);
    if(FAILED(hres))
        return hres;
    args[0] = dp->rgvarg[dp->cArgs - 2];

    hres = dispex_call_builtin(dispex, DISPID_IHTMLELEMENT6_HASATTRIBUTENS, &new_dp, res, ei, caller);
    VariantClear(&args[1]);
    return hres;
}

static HRESULT IHTMLElement6_getAttributeNS_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp,
        VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    VARIANT args[2];
    HRESULT hres;
    DISPPARAMS new_dp = { args, NULL, 2, 0 };

    if(!(flags & DISPATCH_METHOD) || dp->cArgs < 2 || dp->cNamedArgs)
        return S_FALSE;

    switch(V_VT(&dp->rgvarg[dp->cArgs - 1])) {
    case VT_EMPTY:
    case VT_BSTR:
    case VT_NULL:
        return S_FALSE;
    default:
        break;
    }

    hres = change_type(&args[1], &dp->rgvarg[dp->cArgs - 1], VT_BSTR, caller);
    if(FAILED(hres))
        return hres;
    args[0] = dp->rgvarg[dp->cArgs - 2];

    hres = dispex_call_builtin(dispex, DISPID_IHTMLELEMENT6_GETATTRIBUTENS, &new_dp, res, ei, caller);
    VariantClear(&args[1]);
    return hres;
}

static HRESULT IHTMLElement6_setAttributeNS_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp,
        VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    BOOL ns_conv = FALSE, val_conv = FALSE;
    VARIANT args[3];
    HRESULT hres;
    DISPPARAMS new_dp = { args, NULL, 3, 0 };

    if(!(flags & DISPATCH_METHOD) || dp->cArgs < 3 || dp->cNamedArgs)
        return S_FALSE;

    if(dispex_compat_mode(dispex) < COMPAT_MODE_IE10)
        args[2] = dp->rgvarg[dp->cArgs - 1];
    else {
        switch(V_VT(&dp->rgvarg[dp->cArgs - 1])) {
        case VT_EMPTY:
        case VT_BSTR:
        case VT_NULL:
            args[2] = dp->rgvarg[dp->cArgs - 1];
            break;
        default:
            hres = change_type(&args[2], &dp->rgvarg[dp->cArgs - 1], VT_BSTR, caller);
            if(FAILED(hres))
                return hres;
            ns_conv = TRUE;
            break;
        }
    }

    switch(V_VT(&dp->rgvarg[dp->cArgs - 3])) {
    case VT_EMPTY:
    case VT_BSTR:
    case VT_NULL:
        if(!ns_conv)
            return S_FALSE;
        args[0] = dp->rgvarg[dp->cArgs - 3];
        break;
    default:
        hres = change_type(&args[0], &dp->rgvarg[dp->cArgs - 3], VT_BSTR, caller);
        if(FAILED(hres)) {
            if(ns_conv)
                VariantClear(&args[2]);
            return hres;
        }
        val_conv = TRUE;
        break;
    }

    args[1] = dp->rgvarg[dp->cArgs - 2];
    hres = dispex_call_builtin(dispex, DISPID_IHTMLELEMENT6_SETATTRIBUTENS, &new_dp, res, ei, caller);
    if(ns_conv)  VariantClear(&args[2]);
    if(val_conv) VariantClear(&args[0]);
    return hres;
}

static HRESULT IHTMLElement6_removeAttributeNS_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp,
        VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    VARIANT args[2];
    HRESULT hres;
    DISPPARAMS new_dp = { args, NULL, 2, 0 };

    if(!(flags & DISPATCH_METHOD) || dp->cArgs < 2 || dp->cNamedArgs)
        return S_FALSE;

    switch(V_VT(&dp->rgvarg[dp->cArgs - 1])) {
    case VT_EMPTY:
    case VT_BSTR:
    case VT_NULL:
        return S_FALSE;
    default:
        break;
    }

    hres = change_type(&args[1], &dp->rgvarg[dp->cArgs - 1], VT_BSTR, caller);
    if(FAILED(hres))
        return hres;
    args[0] = dp->rgvarg[dp->cArgs - 2];

    hres = dispex_call_builtin(dispex, DISPID_IHTMLELEMENT6_REMOVEATTRIBUTENS, &new_dp, res, ei, caller);
    VariantClear(&args[1]);
    return hres;
}

static HRESULT IHTMLElement6_setAttribute_hook(DispatchEx *dispex, WORD flags, DISPPARAMS *dp,
        VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    VARIANT args[2];
    HRESULT hres;
    DISPPARAMS new_dp = { args, NULL, 2, 0 };

    if(!(flags & DISPATCH_METHOD) || dp->cArgs < 2 || dp->cNamedArgs)
        return S_FALSE;

    switch(V_VT(&dp->rgvarg[dp->cArgs - 2])) {
    case VT_EMPTY:
    case VT_BSTR:
    case VT_NULL:
        return S_FALSE;
    default:
        break;
    }

    hres = change_type(&args[0], &dp->rgvarg[dp->cArgs - 2], VT_BSTR, caller);
    if(FAILED(hres))
        return hres;
    args[1] = dp->rgvarg[dp->cArgs - 1];

    hres = dispex_call_builtin(dispex, DISPID_IHTMLELEMENT6_IE9_SETATTRIBUTE, &new_dp, res, ei, caller);
    VariantClear(&args[0]);
    return hres;
}

void HTMLElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t elem6_ie9_hooks[] = {
        {DISPID_IHTMLELEMENT6_SETATTRIBUTENS, IHTMLElement6_setAttributeNS_hook},
        {DISPID_UNKNOWN}
    };
    static const dispex_hook_t elem6_ie10_hooks[] = {
        {DISPID_IHTMLELEMENT6_HASATTRIBUTENS, IHTMLElement6_hasAttributeNS_hook},
        {DISPID_IHTMLELEMENT6_GETATTRIBUTENS, IHTMLElement6_getAttributeNS_hook},
        {DISPID_IHTMLELEMENT6_SETATTRIBUTENS, IHTMLElement6_setAttributeNS_hook},
        {DISPID_IHTMLELEMENT6_REMOVEATTRIBUTENS, IHTMLElement6_removeAttributeNS_hook},
        {DISPID_IHTMLELEMENT6_IE9_SETATTRIBUTE, IHTMLElement6_setAttribute_hook},
        {DISPID_UNKNOWN}
    };
    static const dispex_hook_t elem2_ie11_hooks[] = {
        {DISPID_IHTMLELEMENT2_ATTACHEVENT, NULL},
        {DISPID_IHTMLELEMENT2_DETACHEVENT, NULL},
        {DISPID_IHTMLELEMENT2_DOSCROLL,    NULL},
        {DISPID_IHTMLELEMENT2_READYSTATE,  NULL},
        {DISPID_UNKNOWN}
    };

    HTMLDOMNode_init_dispex_info(info, mode);

    dispex_info_add_interface(info, IHTMLElement2_tid, mode >= COMPAT_MODE_IE11 ? elem2_ie11_hooks : NULL);

    if(mode >= COMPAT_MODE_IE8)
        dispex_info_add_interface(info, IElementSelector_tid, NULL);

    if(mode >= COMPAT_MODE_IE9) {
        dispex_info_add_interface(info, IHTMLElement6_tid, mode >= COMPAT_MODE_IE10 ? elem6_ie10_hooks : elem6_ie9_hooks);
        dispex_info_add_interface(info, IElementTraversal_tid, NULL);
    }

    if(mode >= COMPAT_MODE_IE10)
    {
        dispex_info_add_interface(info, IHTMLElement7_tid, NULL);
        dispex_info_add_interface(info, IWineHTMLElementPrivate_tid, NULL);
    }
}

static const tid_t HTMLElement_iface_tids[] = {
    HTMLELEMENT_TIDS,
    0
};

static event_target_vtbl_t HTMLElement_event_target_vtbl = {
    {
        NULL,
        HTMLElement_get_dispid,
        HTMLElement_get_name,
        HTMLElement_invoke,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        HTMLElement_populate_props
    },
    NULL,
    HTMLElement_get_gecko_target,
    HTMLElement_bind_event,
    HTMLElement_event_target_dispatch_nsevent_hook,
    HTMLElement_get_parent_event_target,
    HTMLElement_handle_event_default,
    HTMLElement_get_cp_container,
    HTMLElement_set_current_event
};

static void DOMElement_init_dispex_info(dispex_data_t *info, compat_mode_t mode)
{
    static const dispex_hook_t elem_hooks[] = {
        {DISPID_IHTMLELEMENT_CLASSNAME},
        {DISPID_IHTMLELEMENT_ID},
        {DISPID_IHTMLELEMENT_PARENTELEMENT},
        {DISPID_IHTMLELEMENT_STYLE},
        {DISPID_IHTMLELEMENT_ONHELP},
        {DISPID_IHTMLELEMENT_ONCLICK},
        {DISPID_IHTMLELEMENT_ONDBLCLICK},
        {DISPID_IHTMLELEMENT_ONKEYDOWN},
        {DISPID_IHTMLELEMENT_ONKEYUP},
        {DISPID_IHTMLELEMENT_ONKEYPRESS},
        {DISPID_IHTMLELEMENT_ONMOUSEOUT},
        {DISPID_IHTMLELEMENT_ONMOUSEOVER},
        {DISPID_IHTMLELEMENT_ONMOUSEMOVE},
        {DISPID_IHTMLELEMENT_ONMOUSEDOWN},
        {DISPID_IHTMLELEMENT_ONMOUSEUP},
        {DISPID_IHTMLELEMENT_DOCUMENT},
        {DISPID_IHTMLELEMENT_TITLE},
        {DISPID_IHTMLELEMENT_LANGUAGE},
        {DISPID_IHTMLELEMENT_ONSELECTSTART},
        {DISPID_IHTMLELEMENT_SCROLLINTOVIEW},
        {DISPID_IHTMLELEMENT_CONTAINS},
        {DISPID_IHTMLELEMENT_SOURCEINDEX},
        {DISPID_IHTMLELEMENT_RECORDNUMBER},
        {DISPID_IHTMLELEMENT_LANG},
        {DISPID_IHTMLELEMENT_OFFSETLEFT},
        {DISPID_IHTMLELEMENT_OFFSETTOP},
        {DISPID_IHTMLELEMENT_OFFSETWIDTH},
        {DISPID_IHTMLELEMENT_OFFSETHEIGHT},
        {DISPID_IHTMLELEMENT_OFFSETPARENT},
        {DISPID_IHTMLELEMENT_INNERHTML},
        {DISPID_IHTMLELEMENT_INNERTEXT},
        {DISPID_IHTMLELEMENT_OUTERHTML},
        {DISPID_IHTMLELEMENT_OUTERTEXT},
        {DISPID_IHTMLELEMENT_INSERTADJACENTHTML},
        {DISPID_IHTMLELEMENT_INSERTADJACENTTEXT},
        {DISPID_IHTMLELEMENT_PARENTTEXTEDIT},
        {DISPID_IHTMLELEMENT_ISTEXTEDIT},
        {DISPID_IHTMLELEMENT_CLICK},
        {DISPID_IHTMLELEMENT_FILTERS},
        {DISPID_IHTMLELEMENT_ONDRAGSTART},
        {DISPID_IHTMLELEMENT_TOSTRING},
        {DISPID_IHTMLELEMENT_ONBEFOREUPDATE},
        {DISPID_IHTMLELEMENT_ONAFTERUPDATE},
        {DISPID_IHTMLELEMENT_ONERRORUPDATE},
        {DISPID_IHTMLELEMENT_ONROWEXIT},
        {DISPID_IHTMLELEMENT_ONROWENTER},
        {DISPID_IHTMLELEMENT_ONDATASETCHANGED},
        {DISPID_IHTMLELEMENT_ONDATAAVAILABLE},
        {DISPID_IHTMLELEMENT_ONDATASETCOMPLETE},
        {DISPID_IHTMLELEMENT_ONFILTERCHANGE},
        {DISPID_IHTMLELEMENT_CHILDREN},
        {DISPID_IHTMLELEMENT_ALL},
        {DISPID_UNKNOWN}
    };
    static const dispex_hook_t elem2_hooks[] = {
        {DISPID_IHTMLELEMENT2_SCOPENAME},
        {DISPID_IHTMLELEMENT2_SETCAPTURE},
        {DISPID_IHTMLELEMENT2_RELEASECAPTURE},
        {DISPID_IHTMLELEMENT2_ONLOSECAPTURE},
        {DISPID_IHTMLELEMENT2_COMPONENTFROMPOINT},
        {DISPID_IHTMLELEMENT2_DOSCROLL},
        {DISPID_IHTMLELEMENT2_ONSCROLL},
        {DISPID_IHTMLELEMENT2_ONDRAG},
        {DISPID_IHTMLELEMENT2_ONDRAGEND},
        {DISPID_IHTMLELEMENT2_ONDRAGENTER},
        {DISPID_IHTMLELEMENT2_ONDRAGOVER},
        {DISPID_IHTMLELEMENT2_ONDRAGLEAVE},
        {DISPID_IHTMLELEMENT2_ONDROP},
        {DISPID_IHTMLELEMENT2_ONBEFORECUT},
        {DISPID_IHTMLELEMENT2_ONCUT},
        {DISPID_IHTMLELEMENT2_ONBEFORECOPY},
        {DISPID_IHTMLELEMENT2_ONCOPY},
        {DISPID_IHTMLELEMENT2_ONBEFOREPASTE},
        {DISPID_IHTMLELEMENT2_ONPASTE},
        {DISPID_IHTMLELEMENT2_CURRENTSTYLE},
        {DISPID_IHTMLELEMENT2_ONPROPERTYCHANGE},
        {DISPID_IHTMLELEMENT2_SETEXPRESSION},
        {DISPID_IHTMLELEMENT2_GETEXPRESSION},
        {DISPID_IHTMLELEMENT2_REMOVEEXPRESSION},
        {DISPID_IHTMLELEMENT2_TABINDEX},
        {DISPID_IHTMLELEMENT2_FOCUS},
        {DISPID_IHTMLELEMENT2_ACCESSKEY},
        {DISPID_IHTMLELEMENT2_ONBLUR},
        {DISPID_IHTMLELEMENT2_ONFOCUS},
        {DISPID_IHTMLELEMENT2_ONRESIZE},
        {DISPID_IHTMLELEMENT2_BLUR},
        {DISPID_IHTMLELEMENT2_ADDFILTER},
        {DISPID_IHTMLELEMENT2_REMOVEFILTER},
        {DISPID_IHTMLELEMENT2_ATTACHEVENT},
        {DISPID_IHTMLELEMENT2_DETACHEVENT},
        {DISPID_IHTMLELEMENT2_READYSTATE},
        {DISPID_IHTMLELEMENT2_ONREADYSTATECHANGE},
        {DISPID_IHTMLELEMENT2_ONROWSDELETE},
        {DISPID_IHTMLELEMENT2_ONROWSINSERTED},
        {DISPID_IHTMLELEMENT2_ONCELLCHANGE},
        {DISPID_IHTMLELEMENT2_DIR},
        {DISPID_IHTMLELEMENT2_CREATECONTROLRANGE},
        {DISPID_IHTMLELEMENT2_CLEARATTRIBUTES},
        {DISPID_IHTMLELEMENT2_MERGEATTRIBUTES},
        {DISPID_IHTMLELEMENT2_ONCONTEXTMENU},
        {DISPID_IHTMLELEMENT2_INSERTADJACENTELEMENT},
        {DISPID_IHTMLELEMENT2_APPLYELEMENT},
        {DISPID_IHTMLELEMENT2_GETADJACENTTEXT},
        {DISPID_IHTMLELEMENT2_REPLACEADJACENTTEXT},
        {DISPID_IHTMLELEMENT2_CANHAVECHILDREN},
        {DISPID_IHTMLELEMENT2_ADDBEHAVIOR},
        {DISPID_IHTMLELEMENT2_REMOVEBEHAVIOR},
        {DISPID_IHTMLELEMENT2_RUNTIMESTYLE},
        {DISPID_IHTMLELEMENT2_BEHAVIORURNS},
        {DISPID_IHTMLELEMENT2_TAGURN},
        {DISPID_IHTMLELEMENT2_ONBEFOREEDITFOCUS},
        {DISPID_IHTMLELEMENT2_READYSTATEVALUE},
        {DISPID_UNKNOWN}
    };
    static const dispex_hook_t elem3_hooks[] = {
        {DISPID_IHTMLELEMENT3_MERGEATTRIBUTES},
        {DISPID_IHTMLELEMENT3_ISMULTILINE},
        {DISPID_IHTMLELEMENT3_CANHAVEHTML},
        {DISPID_IHTMLELEMENT3_ONLAYOUTCOMPLETE},
        {DISPID_IHTMLELEMENT3_ONPAGE},
        {DISPID_IHTMLELEMENT3_INFLATEBLOCK},
        {DISPID_IHTMLELEMENT3_ONBEFOREDEACTIVATE},
        {DISPID_IHTMLELEMENT3_SETACTIVE},
        {DISPID_IHTMLELEMENT3_CONTENTEDITABLE},
        {DISPID_IHTMLELEMENT3_ISCONTENTEDITABLE},
        {DISPID_IHTMLELEMENT3_HIDEFOCUS},
        {DISPID_IHTMLELEMENT3_DISABLED},
        {DISPID_IHTMLELEMENT3_ISDISABLED},
        {DISPID_IHTMLELEMENT3_ONMOVE},
        {DISPID_IHTMLELEMENT3_ONCONTROLSELECT},
        {DISPID_IHTMLELEMENT3_FIREEVENT},
        {DISPID_IHTMLELEMENT3_ONRESIZESTART},
        {DISPID_IHTMLELEMENT3_ONRESIZEEND},
        {DISPID_IHTMLELEMENT3_ONMOVESTART},
        {DISPID_IHTMLELEMENT3_ONMOVEEND},
        {DISPID_IHTMLELEMENT3_ONMOUSEENTER},
        {DISPID_IHTMLELEMENT3_ONMOUSELEAVE},
        {DISPID_IHTMLELEMENT3_ONACTIVATE},
        {DISPID_IHTMLELEMENT3_ONDEACTIVATE},
        {DISPID_IHTMLELEMENT3_DRAGDROP},
        {DISPID_IHTMLELEMENT3_GLYPHMODE},
        {DISPID_UNKNOWN}
    };
    static const dispex_hook_t elem4_hooks[] = {
        {DISPID_IHTMLELEMENT4_ONMOUSEWHEEL},
        {DISPID_IHTMLELEMENT4_NORMALIZE},
        {DISPID_IHTMLELEMENT4_ONBEFOREACTIVATE},
        {DISPID_IHTMLELEMENT4_ONFOCUSIN},
        {DISPID_IHTMLELEMENT4_ONFOCUSOUT},
        {DISPID_UNKNOWN}
    };
    static const dispex_hook_t elem6_ie10_hooks[] = {
        {DISPID_IHTMLELEMENT6_HASATTRIBUTENS, IHTMLElement6_hasAttributeNS_hook},
        {DISPID_IHTMLELEMENT6_GETATTRIBUTENS, IHTMLElement6_getAttributeNS_hook},
        {DISPID_IHTMLELEMENT6_REMOVEATTRIBUTENS, IHTMLElement6_removeAttributeNS_hook},
        {DISPID_IHTMLELEMENT6_IE9_SETATTRIBUTE, IHTMLElement6_setAttribute_hook},

        /* common for all supported modes */
        {DISPID_IHTMLELEMENT6_SETATTRIBUTENS, IHTMLElement6_setAttributeNS_hook},

        {DISPID_IHTMLELEMENT6_IE9_NODENAME},
        {DISPID_IHTMLELEMENT6_GETELEMENTSBYCLASSNAME},
        {DISPID_IHTMLELEMENT6_ONABORT},
        {DISPID_IHTMLELEMENT6_ONCANPLAY},
        {DISPID_IHTMLELEMENT6_ONCANPLAYTHROUGH},
        {DISPID_IHTMLELEMENT6_ONCHANGE},
        {DISPID_IHTMLELEMENT6_ONDURATIONCHANGE},
        {DISPID_IHTMLELEMENT6_ONEMPTIED},
        {DISPID_IHTMLELEMENT6_ONENDED},
        {DISPID_IHTMLELEMENT6_ONERROR},
        {DISPID_IHTMLELEMENT6_ONINPUT},
        {DISPID_IHTMLELEMENT6_ONLOAD},
        {DISPID_IHTMLELEMENT6_ONLOADEDDATA},
        {DISPID_IHTMLELEMENT6_ONLOADEDMETADATA},
        {DISPID_IHTMLELEMENT6_ONLOADSTART},
        {DISPID_IHTMLELEMENT6_ONPAUSE},
        {DISPID_IHTMLELEMENT6_ONPLAY},
        {DISPID_IHTMLELEMENT6_ONPLAYING},
        {DISPID_IHTMLELEMENT6_ONPROGRESS},
        {DISPID_IHTMLELEMENT6_ONRATECHANGE},
        {DISPID_IHTMLELEMENT6_ONRESET},
        {DISPID_IHTMLELEMENT6_ONSEEKED},
        {DISPID_IHTMLELEMENT6_ONSEEKING},
        {DISPID_IHTMLELEMENT6_ONSELECT},
        {DISPID_IHTMLELEMENT6_ONSTALLED},
        {DISPID_IHTMLELEMENT6_ONSUBMIT},
        {DISPID_IHTMLELEMENT6_ONSUSPEND},
        {DISPID_IHTMLELEMENT6_ONTIMEUPDATE},
        {DISPID_IHTMLELEMENT6_ONVOLUMECHANGE},
        {DISPID_IHTMLELEMENT6_ONWAITING},
        {DISPID_IHTMLELEMENT6_IE9_HASATTRIBUTES},
        {DISPID_UNKNOWN}
    };
    const dispex_hook_t *const elem6_hooks = elem6_ie10_hooks + 4;
    static const dispex_hook_t elem7_hooks[] = {
        {DISPID_IHTMLELEMENT7_ONMSPOINTERHOVER},
        {DISPID_IHTMLELEMENT7_ONMSTRANSITIONSTART},
        {DISPID_IHTMLELEMENT7_ONMSTRANSITIONEND},
        {DISPID_IHTMLELEMENT7_ONMSANIMATIONSTART},
        {DISPID_IHTMLELEMENT7_ONMSANIMATIONEND},
        {DISPID_IHTMLELEMENT7_ONMSANIMATIONITERATION},
        {DISPID_IHTMLELEMENT7_ONINVALID},
        {DISPID_IHTMLELEMENT7_XMSACCELERATORKEY},
        {DISPID_IHTMLELEMENT7_SPELLCHECK},
        {DISPID_IHTMLELEMENT7_ONMSMANIPULATIONSTATECHANGED},
        {DISPID_IHTMLELEMENT7_ONCUECHANGE},
        {DISPID_UNKNOWN}
    };
    static const tid_t hookless_tids[] = {
        IHTMLDOMNode_tid,
        IHTMLDOMNode2_tid,
        IElementSelector_tid,
        IElementTraversal_tid,
        0
    };
    unsigned i;

    dispex_info_add_interface(info, IHTMLElement2_tid, elem2_hooks);
    dispex_info_add_interface(info, IHTMLElement6_tid, mode >= COMPAT_MODE_IE10 ? elem6_ie10_hooks : elem6_hooks);
    if(mode >= COMPAT_MODE_IE10)
        dispex_info_add_interface(info, IHTMLElement7_tid, elem7_hooks);
    dispex_info_add_interface(info, IHTMLElement_tid, elem_hooks);
    dispex_info_add_interface(info, IHTMLElement3_tid, elem3_hooks);
    dispex_info_add_interface(info, IHTMLElement4_tid, elem4_hooks);
    for(i = 0; i < ARRAY_SIZE(hookless_tids); i++)
        dispex_info_add_interface(info, hookless_tids[i], NULL);
}

dispex_static_data_t DOMElement_dispex = {
    L"Element",
    NULL,
    PROTO_ID_DOMElement,
    NULL_tid,
    no_iface_tids,
    DOMElement_init_dispex_info
};

struct token_list {
    DispatchEx dispex;
    IWineDOMTokenList IWineDOMTokenList_iface;
    IHTMLElement *element;

    LONG ref;
};

static inline struct token_list *impl_from_IWineDOMTokenList(IWineDOMTokenList *iface)
{
    return CONTAINING_RECORD(iface, struct token_list, IWineDOMTokenList_iface);
}

static HRESULT WINAPI token_list_QueryInterface(IWineDOMTokenList *iface, REFIID riid, void **ppv)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);

    TRACE("(%p)->(%s %p)\n", token_list, debugstr_mshtml_guid(riid), ppv);

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        *ppv = &token_list->IWineDOMTokenList_iface;
    }else if(IsEqualGUID(&IID_IWineDOMTokenList, riid)) {
        *ppv = &token_list->IWineDOMTokenList_iface;
    }else if(dispex_query_interface(&token_list->dispex, riid, ppv)) {
        return *ppv ? S_OK : E_NOINTERFACE;
    }else {
        WARN("(%p)->(%s %p)\n", token_list, debugstr_mshtml_guid(riid), ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI token_list_AddRef(IWineDOMTokenList *iface)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);
    LONG ref = InterlockedIncrement(&token_list->ref);

    TRACE("(%p) ref=%ld\n", token_list, ref);

    return ref;
}

static ULONG WINAPI token_list_Release(IWineDOMTokenList *iface)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);
    LONG ref = InterlockedDecrement(&token_list->ref);

    TRACE("(%p) ref=%ld\n", token_list, ref);

    if(!ref) {
        IHTMLElement_Release(token_list->element);
        release_dispex(&token_list->dispex);
        free(token_list);
    }

    return ref;
}

static HRESULT WINAPI token_list_GetTypeInfoCount(IWineDOMTokenList *iface, UINT *pctinfo)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);
    FIXME("(%p)->(%p)\n", token_list, pctinfo);
    return E_NOTIMPL;
}

static HRESULT WINAPI token_list_GetTypeInfo(IWineDOMTokenList *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);

    return IDispatchEx_GetTypeInfo(&token_list->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI token_list_GetIDsOfNames(IWineDOMTokenList *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);

    return IDispatchEx_GetIDsOfNames(&token_list->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI token_list_Invoke(IWineDOMTokenList *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
        VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);

    return IDispatchEx_Invoke(&token_list->dispex.IDispatchEx_iface, dispIdMember, riid, lcid, wFlags,
            pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static const WCHAR *find_token(const WCHAR *list, const WCHAR *token, unsigned int token_len)
{
    const WCHAR *ptr, *next;

    if (!list || !token)
        return NULL;

    ptr = list;
    while (*ptr)
    {
        while (iswspace(*ptr))
            ++ptr;
        if (!*ptr)
            break;
        next = ptr + 1;
        while (*next && !iswspace(*next))
            ++next;

        if (next - ptr == token_len && !wcsncmp(ptr, token, token_len))
            return ptr;
        ptr = next;
    }
    return NULL;
}

static HRESULT token_list_add_remove(IWineDOMTokenList *iface, BSTR token, BOOL remove)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);
    unsigned int i, len, old_len, new_len;
    const WCHAR *old_pos;
    BSTR new, old;
    HRESULT hr;

    TRACE("iface %p, token %s, remove %#x.\n", iface, debugstr_w(token), remove);

    len = token ? lstrlenW(token) : 0;
    if (!len)
    {
        WARN("Empty token.\n");
        return E_INVALIDARG;
    }

    for (i = 0; i < len; ++i)
        if (iswspace(token[i]))
        {
            WARN("Token has spaces.\n");
            return E_INVALIDARG;
        }

    if (FAILED(hr = IHTMLElement_get_className(token_list->element, &old)))
        return hr;

    TRACE("old %s.\n", debugstr_w(old));

    if (((old_pos = find_token(old, token, len)) && !remove)
            || (!old_pos && remove))
    {
        SysFreeString(old);
        return S_OK;
    }

    old_len = old ? lstrlenW(old) : 0;
    if (remove)
    {
        while (old_pos != old && iswspace(old_pos[-1]))
        {
            --old_pos;
            ++len;
        }
        while (iswspace(old_pos[len]))
            ++len;

        if (old_pos != old && old_pos[len])
            --len;

        new_len = old_len - len;
    }
    else
    {
        new_len = old_len + len + !!old_len;
    }

    if (!(new = SysAllocStringLen(NULL, new_len)))
    {
        ERR("No memory.\n");
        SysFreeString(old);
        return E_OUTOFMEMORY;
    }

    if (remove)
    {
        memcpy(new, old, sizeof(*new) * (old_pos - old));
        memcpy(new + (old_pos - old), old_pos + len, sizeof(*new) * (old_len - (old_pos - old) - len + 1));
    }
    else
    {
        memcpy(new, old, sizeof(*new) * old_len);
        if (old_len)
            new[old_len++]= L' ';
        memcpy(new + old_len, token, sizeof(*new) * len);
        new[old_len + len] = 0;
    }

    SysFreeString(old);

    TRACE("new %s.\n", debugstr_w(new));

    hr = IHTMLElement_put_className(token_list->element, new);
    SysFreeString(new);
    return hr;
}

static HRESULT WINAPI token_list_add(IWineDOMTokenList *iface, BSTR token)
{
    return token_list_add_remove(iface, token, FALSE);
}

static HRESULT WINAPI token_list_remove(IWineDOMTokenList *iface, BSTR token)
{
    return token_list_add_remove(iface, token, TRUE);
}

static HRESULT WINAPI token_list_toString(IWineDOMTokenList *iface, BSTR *String)
{
    struct token_list *token_list = impl_from_IWineDOMTokenList(iface);

    TRACE("(%p)->(%p)\n", token_list, String);

    return IHTMLElement_get_className(token_list->element, String);
}

static const IWineDOMTokenListVtbl WineDOMTokenListVtbl = {
    token_list_QueryInterface,
    token_list_AddRef,
    token_list_Release,
    token_list_GetTypeInfoCount,
    token_list_GetTypeInfo,
    token_list_GetIDsOfNames,
    token_list_Invoke,
    token_list_add,
    token_list_remove,
    token_list_toString
};

static inline struct token_list *token_list_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, struct token_list, dispex);
}

static HRESULT token_list_value(DispatchEx *dispex, LCID lcid, WORD flags, DISPPARAMS *params,
        VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    struct token_list *token_list = token_list_from_DispatchEx(dispex);
    HRESULT hres;

    switch(flags) {
    case DISPATCH_PROPERTYGET:
        hres = IHTMLElement_get_className(token_list->element, &V_BSTR(res));
        if(FAILED(hres))
            return hres;
        V_VT(res) = VT_BSTR;
        break;
    default:
        FIXME("Unimplemented flags %x\n", flags);
        return E_NOTIMPL;
    }

    return S_OK;
}

static const dispex_static_data_vtbl_t token_list_dispex_vtbl = {
    token_list_value
};

static const tid_t token_list_iface_tids[] = {
    IWineDOMTokenList_tid,
    0
};
dispex_static_data_t DOMTokenList_dispex = {
    L"DOMTokenList",
    &token_list_dispex_vtbl,
    PROTO_ID_DOMTokenList,
    IWineDOMTokenList_tid,
    token_list_iface_tids
};

static HRESULT create_token_list(compat_mode_t compat_mode, HTMLElement *element, IWineDOMTokenList **ret)
{
    struct token_list *obj;

    obj = calloc(1, sizeof(*obj));
    if(!obj)
    {
        ERR("No memory.\n");
        return E_OUTOFMEMORY;
    }

    obj->IWineDOMTokenList_iface.lpVtbl = &WineDOMTokenListVtbl;
    obj->ref = 1;
    init_dispatch(&obj->dispex, (IUnknown*)&obj->IWineDOMTokenList_iface, &DOMTokenList_dispex,
                  get_inner_window(element->node.doc), compat_mode);
    IHTMLElement_AddRef(&element->IHTMLElement_iface);
    obj->element = &element->IHTMLElement_iface;

    *ret = &obj->IWineDOMTokenList_iface;
    return S_OK;
}

static inline HTMLElement *impl_from_IWineHTMLElementPrivateVtbl(IWineHTMLElementPrivate *iface)
{
    return CONTAINING_RECORD(iface, HTMLElement, IWineHTMLElementPrivate_iface);
}

static HRESULT WINAPI htmlelement_private_QueryInterface(IWineHTMLElementPrivate *iface,
        REFIID riid, void **ppv)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    return IHTMLElement_QueryInterface(&This->IHTMLElement_iface, riid, ppv);
}

static ULONG WINAPI htmlelement_private_AddRef(IWineHTMLElementPrivate *iface)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    return IHTMLElement_AddRef(&This->IHTMLElement_iface);
}

static ULONG WINAPI htmlelement_private_Release(IWineHTMLElementPrivate *iface)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    return IHTMLElement_Release(&This->IHTMLElement_iface);
}

static HRESULT WINAPI htmlelement_private_GetTypeInfoCount(IWineHTMLElementPrivate *iface, UINT *pctinfo)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    return HTMLElement_GetTypeInfoCount(&This->IHTMLElement_iface, pctinfo);
}

static HRESULT WINAPI htmlelement_private_GetTypeInfo(IWineHTMLElementPrivate *iface, UINT iTInfo,
                                              LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    return HTMLElement_GetTypeInfo(&This->IHTMLElement_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI htmlelement_private_GetIDsOfNames(IWineHTMLElementPrivate *iface, REFIID riid,
                                                LPOLESTR *rgszNames, UINT cNames,
                                                LCID lcid, DISPID *rgDispId)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    return HTMLElement_GetIDsOfNames(&This->IHTMLElement_iface, riid, rgszNames, cNames, lcid,
            rgDispId);
}

static HRESULT WINAPI htmlelement_private_Invoke(IWineHTMLElementPrivate *iface, DISPID dispIdMember,
                            REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                            VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    return HTMLElement_Invoke(&This->IHTMLElement_iface, dispIdMember, riid, lcid, wFlags,
            pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI htmlelement_private_get_classList(IWineHTMLElementPrivate *iface, IDispatch **class_list)
{
    HTMLElement *This = impl_from_IWineHTMLElementPrivateVtbl(iface);

    TRACE("iface %p, class_list %p.\n", iface, class_list);

    return create_token_list(dispex_compat_mode(&This->node.event_target.dispex), This,
            (IWineDOMTokenList **)class_list);
}

static const IWineHTMLElementPrivateVtbl WineHTMLElementPrivateVtbl = {
    htmlelement_private_QueryInterface,
    htmlelement_private_AddRef,
    htmlelement_private_Release,
    htmlelement_private_GetTypeInfoCount,
    htmlelement_private_GetTypeInfo,
    htmlelement_private_GetIDsOfNames,
    htmlelement_private_Invoke,
    htmlelement_private_get_classList,
};

dispex_static_data_t HTMLElement_dispex = {
    L"HTMLElement",
    &HTMLElement_event_target_vtbl.dispex_vtbl,
    PROTO_ID_HTMLElement,
    DispHTMLUnknownElement_tid,
    HTMLElement_iface_tids,
    HTMLElement_init_dispex_info
};

dispex_static_data_t HTMLUnknownElement_dispex = {
    L"HTMLUnknownElement",
    &HTMLElement_event_target_vtbl.dispex_vtbl,
    PROTO_ID_HTMLUnknownElement,
    DispHTMLUnknownElement_tid,
    HTMLElement_iface_tids,
    HTMLElement_init_dispex_info
};

void HTMLElement_Init(HTMLElement *This, HTMLDocumentNode *doc, nsIDOMElement *nselem, dispex_static_data_t *dispex_data)
{
    This->IHTMLElement_iface.lpVtbl = &HTMLElementVtbl;
    This->IHTMLElement2_iface.lpVtbl = &HTMLElement2Vtbl;
    This->IHTMLElement3_iface.lpVtbl = &HTMLElement3Vtbl;
    This->IHTMLElement4_iface.lpVtbl = &HTMLElement4Vtbl;
    This->IHTMLElement6_iface.lpVtbl = &HTMLElement6Vtbl;
    This->IHTMLElement7_iface.lpVtbl = &HTMLElement7Vtbl;
    This->IHTMLUniqueName_iface.lpVtbl = &HTMLUniqueNameVtbl;
    This->IElementSelector_iface.lpVtbl = &ElementSelectorVtbl;
    This->IElementTraversal_iface.lpVtbl = &ElementTraversalVtbl;
    This->IProvideMultipleClassInfo_iface.lpVtbl = &ProvideMultipleClassInfoVtbl;
    This->IWineHTMLElementPrivate_iface.lpVtbl = &WineHTMLElementPrivateVtbl;

    if(dispex_data && !dispex_data->vtbl)
        dispex_data->vtbl = &HTMLElement_event_target_vtbl.dispex_vtbl;

    if(nselem) {
        nsIDOMHTMLElement *html_element;
        nsresult nsres;

        if(!dispex_data)
            dispex_data = (doc->document_mode >= COMPAT_MODE_IE9) ? &HTMLGenericElement_dispex : &HTMLUnknownElement_dispex;

        HTMLDOMNode_Init(doc, &This->node, (nsIDOMNode*)nselem, dispex_data);

        /* No AddRef, share reference with HTMLDOMNode */
        assert((nsIDOMNode*)nselem == This->node.nsnode);
        This->dom_element = nselem;

        nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMHTMLElement, (void**)&html_element);
        if(NS_SUCCEEDED(nsres)) {
            This->html_element = html_element;
            /* share reference with HTMLDOMNode */
            assert((nsIDOMNode*)html_element == This->node.nsnode);
            nsIDOMHTMLElement_Release(html_element);
        }
    }

    ConnectionPointContainer_Init(&This->cp_container, (IUnknown*)&This->IHTMLElement_iface, This->node.vtbl->cpc_entries);
}

HRESULT HTMLElement_Create(HTMLDocumentNode *doc, nsIDOMNode *nsnode, BOOL use_generic, HTMLElement **ret)
{
    nsIDOMHTMLElement *nshtmlelem;
    nsIDOMSVGElement *svg_element;
    nsIDOMElement *nselem;
    nsAString tag_name_str;
    const PRUnichar *tag_name;
    const tag_desc_t *tag;
    HTMLElement *elem;
    nsresult nsres;
    HRESULT hres;

    nsres = nsIDOMNode_QueryInterface(nsnode, &IID_nsIDOMElement, (void**)&nselem);
    if(NS_FAILED(nsres)) {
        ERR("no nsIDOMElement iface\n");
        return E_FAIL;
    }

    nsAString_Init(&tag_name_str, NULL);
    nsIDOMElement_GetTagName(nselem, &tag_name_str);

    nsAString_GetData(&tag_name_str, &tag_name);

    /* Check this first, as Gecko treats svg elements as non-HTML */
    nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMSVGElement, (void**)&svg_element);
    if(NS_SUCCEEDED(nsres)) {
        hres = create_svg_element(doc, svg_element, tag_name, &elem);
        nsIDOMSVGElement_Release(svg_element);
        goto done;
    }

    nsres = nsIDOMElement_QueryInterface(nselem, &IID_nsIDOMHTMLElement, (void**)&nshtmlelem);
    if(NS_FAILED(nsres)) {
        if(!(elem = calloc(1, sizeof(HTMLElement))))
            hres = E_OUTOFMEMORY;
        else {
            elem->node.vtbl = &HTMLElementImplVtbl;
            HTMLElement_Init(elem, doc, nselem, &DOMElement_dispex);
            hres = S_OK;
        }
        goto done;
    }
    nsIDOMHTMLElement_Release(nshtmlelem);

    tag = get_tag_desc(tag_name);
    if(tag) {
        hres = tag->constructor(doc, nselem, &elem);
    }else {
        if(use_generic || doc->document_mode >= COMPAT_MODE_IE9) {
            hres = HTMLGenericElement_Create(doc, nselem, &elem);
        }else {
            elem = calloc(1, sizeof(HTMLElement));
            if(elem) {
                elem->node.vtbl = &HTMLElementImplVtbl;
                HTMLElement_Init(elem, doc, nselem, &HTMLUnknownElement_dispex);
                hres = S_OK;
            }else {
                hres = E_OUTOFMEMORY;
            }
        }
    }

done:
    TRACE("%s ret %p\n", debugstr_w(tag_name), elem);

    nsIDOMElement_Release(nselem);
    nsAString_Finish(&tag_name_str);
    if(FAILED(hres))
        return hres;

    *ret = elem;
    return S_OK;
}

static HRESULT HTMLElement_Ctor(HTMLDocumentNode *doc, nsIDOMElement *nselem, HTMLElement **elem)
{
    HTMLElement *ret;

    ret = calloc(1, sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->node.vtbl = &HTMLElementImplVtbl;
    HTMLElement_Init(ret, doc, nselem, &HTMLElement_dispex);

    *elem = ret;
    return S_OK;
}

HRESULT get_element(nsIDOMElement *nselem, HTMLElement **ret)
{
    HTMLDOMNode *node;
    HRESULT hres;

    hres = get_node((nsIDOMNode*)nselem, TRUE, &node);
    if(FAILED(hres))
        return hres;

    *ret = impl_from_HTMLDOMNode(node);
    return S_OK;
}

/* interface IHTMLFiltersCollection */
static HRESULT WINAPI HTMLFiltersCollection_QueryInterface(IHTMLFiltersCollection *iface, REFIID riid, void **ppv)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);

    TRACE("%p %s %p\n", This, debugstr_mshtml_guid(riid), ppv );

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        *ppv = &This->IHTMLFiltersCollection_iface;
    }else if(IsEqualGUID(&IID_IHTMLFiltersCollection, riid)) {
        TRACE("(%p)->(IID_IHTMLFiltersCollection %p)\n", This, ppv);
        *ppv = &This->IHTMLFiltersCollection_iface;
    }else if(dispex_query_interface(&This->dispex, riid, ppv)) {
        return *ppv ? S_OK : E_NOINTERFACE;
    }else {
        *ppv = NULL;
        FIXME("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI HTMLFiltersCollection_AddRef(IHTMLFiltersCollection *iface)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI HTMLFiltersCollection_Release(IHTMLFiltersCollection *iface)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref)
    {
        free(This);
    }

    return ref;
}

static HRESULT WINAPI HTMLFiltersCollection_GetTypeInfoCount(IHTMLFiltersCollection *iface, UINT *pctinfo)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    return IDispatchEx_GetTypeInfoCount(&This->dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLFiltersCollection_GetTypeInfo(IHTMLFiltersCollection *iface,
                                    UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLFiltersCollection_GetIDsOfNames(IHTMLFiltersCollection *iface,
                                    REFIID riid, LPOLESTR *rgszNames, UINT cNames,
                                    LCID lcid, DISPID *rgDispId)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLFiltersCollection_Invoke(IHTMLFiltersCollection *iface, DISPID dispIdMember, REFIID riid,
                                    LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult,
                                    EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLFiltersCollection_get_length(IHTMLFiltersCollection *iface, LONG *p)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);

    if(!p)
        return E_POINTER;

    FIXME("(%p)->(%p) Always returning 0\n", This, p);
    *p = 0;

    return S_OK;
}

static HRESULT WINAPI HTMLFiltersCollection_get__newEnum(IHTMLFiltersCollection *iface, IUnknown **p)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    FIXME("(%p)->(%p)\n", This, p);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLFiltersCollection_item(IHTMLFiltersCollection *iface, VARIANT *pvarIndex, VARIANT *pvarResult)
{
    HTMLFiltersCollection *This = impl_from_IHTMLFiltersCollection(iface);
    FIXME("(%p)->(%p, %p)\n", This, pvarIndex, pvarResult);
    return E_NOTIMPL;
}

static const IHTMLFiltersCollectionVtbl HTMLFiltersCollectionVtbl = {
    HTMLFiltersCollection_QueryInterface,
    HTMLFiltersCollection_AddRef,
    HTMLFiltersCollection_Release,
    HTMLFiltersCollection_GetTypeInfoCount,
    HTMLFiltersCollection_GetTypeInfo,
    HTMLFiltersCollection_GetIDsOfNames,
    HTMLFiltersCollection_Invoke,
    HTMLFiltersCollection_get_length,
    HTMLFiltersCollection_get__newEnum,
    HTMLFiltersCollection_item
};

static HRESULT HTMLFiltersCollection_get_dispid(DispatchEx *dispex, BSTR name, DWORD flags, DISPID *dispid)
{
    WCHAR *ptr;
    int idx = 0;

    for(ptr = name; *ptr && is_digit(*ptr); ptr++)
        idx = idx*10 + (*ptr-'0');
    if(*ptr)
        return DISP_E_UNKNOWNNAME;

    *dispid = MSHTML_DISPID_CUSTOM_MIN + idx;
    TRACE("ret %lx\n", *dispid);
    return S_OK;
}

static HRESULT HTMLFiltersCollection_get_name(DispatchEx *dispex, DISPID id, BSTR *name)
{
    DWORD idx = id - MSHTML_DISPID_CUSTOM_MIN;
    WCHAR buf[11];
    UINT len;

    len = swprintf(buf, ARRAY_SIZE(buf), L"%u", idx);
    return (*name = SysAllocStringLen(buf, len)) ? S_OK : E_OUTOFMEMORY;
}

static HRESULT HTMLFiltersCollection_invoke(DispatchEx *dispex, IDispatch *this_obj, DISPID id, LCID lcid, WORD flags,
        DISPPARAMS *params, VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    TRACE("(%p)->(%lx %lx %x %p %p %p)\n", dispex, id, lcid, flags, params, res, ei);

    V_VT(res) = VT_DISPATCH;
    V_DISPATCH(res) = NULL;

    FIXME("always returning NULL\n");

    return S_OK;
}

static const dispex_static_data_vtbl_t HTMLFiltersCollection_dispex_vtbl = {
    NULL,
    HTMLFiltersCollection_get_dispid,
    HTMLFiltersCollection_get_name,
    HTMLFiltersCollection_invoke,
    NULL
};

static const tid_t HTMLFiltersCollection_iface_tids[] = {
    IHTMLFiltersCollection_tid,
    0
};
static dispex_static_data_t HTMLFiltersCollection_dispex = {
    L"FiltersCollection",
    &HTMLFiltersCollection_dispex_vtbl,
    PROTO_ID_NULL,
    IHTMLFiltersCollection_tid,
    HTMLFiltersCollection_iface_tids
};

static HRESULT create_filters_collection(HTMLElement *elem, IHTMLFiltersCollection **ret)
{
    compat_mode_t compat_mode = dispex_compat_mode(&elem->node.event_target.dispex);
    HTMLFiltersCollection *collection;

    if(!(collection = malloc(sizeof(HTMLFiltersCollection))))
        return E_OUTOFMEMORY;

    collection->IHTMLFiltersCollection_iface.lpVtbl = &HTMLFiltersCollectionVtbl;
    collection->ref = 1;

    init_dispatch(&collection->dispex, (IUnknown*)&collection->IHTMLFiltersCollection_iface,
                  &HTMLFiltersCollection_dispex, get_inner_window(elem->node.doc),
                  min(compat_mode, COMPAT_MODE_IE8));

    *ret = &collection->IHTMLFiltersCollection_iface;
    return S_OK;
}

static HRESULT get_attr_dispid_by_relative_idx(HTMLAttributeCollection *This, LONG *idx, DISPID start,
        DISPID *dispid, nsIDOMAttr **nsattr)
{
    IDispatchEx *dispex = &This->elem->node.event_target.dispex.IDispatchEx_iface;
    DISPID id = start;
    UINT32 length;
    LONG len = -1;
    HRESULT hres;

    if(This->nsattrs) {
        id++;
        if(dispid) {
            *dispid = id + *idx;
            if(!nsattr)
                return S_OK;
            if(NS_FAILED(nsIDOMMozNamedAttrMap_Item(This->nsattrs, *dispid, nsattr)))
                return E_FAIL;
            return *nsattr ? S_OK : DISP_E_UNKNOWNNAME;
        }
        nsIDOMMozNamedAttrMap_GetLength(This->nsattrs, &length);
        *idx = (length > id) ? length - id : 0;
        return S_OK;
    }

    FIXME("filter non-enumerable attributes out\n");

    while(1) {
        hres = IDispatchEx_GetNextDispID(dispex, fdexEnumAll, id, &id);
        if(FAILED(hres))
            return hres;
        else if(hres == S_FALSE)
            break;

        len++;
        if(len == *idx)
            break;
    }

    if(dispid) {
        *dispid = id;
        return *idx==len ? S_OK : DISP_E_UNKNOWNNAME;
    }

    *idx = len+1;
    return S_OK;
}

static HRESULT get_attr_dispid_by_idx(HTMLAttributeCollection *This, LONG *idx, DISPID *dispid, nsIDOMAttr **nsattr)
{
    return get_attr_dispid_by_relative_idx(This, idx, DISPID_STARTENUM, dispid, nsattr);
}

static inline HRESULT get_attr_dispid_by_name(HTMLAttributeCollection *This, BSTR name, DISPID *id, nsIDOMAttr **nsattr)
{
    nsAString nsstr;
    nsresult nsres;
    HRESULT hres;

    if(name[0]>='0' && name[0]<='9') {
        WCHAR *end_ptr;
        LONG idx;

        idx = wcstoul(name, &end_ptr, 10);
        if(!*end_ptr) {
            hres = get_attr_dispid_by_idx(This, &idx, id, nsattr);
            if(SUCCEEDED(hres))
                return hres;
        }
    }

    if(This->nsattrs) {
        nsAString_InitDepend(&nsstr, name);
        *nsattr = NULL;
        nsres = nsIDOMMozNamedAttrMap_GetNamedItem(This->nsattrs, &nsstr, nsattr);
        nsAString_Finish(&nsstr);
        if(NS_FAILED(nsres))
            return E_FAIL;
        return *nsattr ? S_OK : DISP_E_UNKNOWNNAME;
    }

    if(!This->elem) {
        WARN("NULL elem\n");
        return E_UNEXPECTED;
    }

    hres = IDispatchEx_GetDispID(&This->elem->node.event_target.dispex.IDispatchEx_iface,
            name, fdexNameCaseInsensitive, id);
    if(FAILED(hres))
        return hres;
    return dispex_is_builtin_method(&This->elem->node.event_target.dispex, *id) ? DISP_E_UNKNOWNNAME : S_OK;
}

static inline HRESULT get_domattr(HTMLAttributeCollection *This, DISPID id, nsIDOMAttr *nsattr,
        LONG *list_pos, HTMLDOMAttribute **attr)
{
    HRESULT hres;

    if(!(*attr = find_attr_in_list(This, id, nsattr, list_pos))) {
        if(!This->elem) {
            WARN("NULL elem\n");
            return E_UNEXPECTED;
        }

        if(nsattr)
            id = get_dispid_for_nsattr(This->elem, nsattr);
        hres = HTMLDOMAttribute_Create(NULL, This->elem->node.doc, This->elem, id, nsattr,
                                       dispex_compat_mode(&This->elem->node.event_target.dispex), attr);
        if(FAILED(hres))
            return hres;
    }

    IHTMLDOMAttribute_AddRef(&(*attr)->IHTMLDOMAttribute_iface);
    return S_OK;
}

typedef struct {
    IEnumVARIANT IEnumVARIANT_iface;

    LONG ref;

    ULONG iter;
    DISPID iter_dispid;
    HTMLAttributeCollection *col;
} HTMLAttributeCollectionEnum;

static inline HTMLAttributeCollectionEnum *HTMLAttributeCollectionEnum_from_IEnumVARIANT(IEnumVARIANT *iface)
{
    return CONTAINING_RECORD(iface, HTMLAttributeCollectionEnum, IEnumVARIANT_iface);
}

static HRESULT WINAPI HTMLAttributeCollectionEnum_QueryInterface(IEnumVARIANT *iface, REFIID riid, void **ppv)
{
    HTMLAttributeCollectionEnum *This = HTMLAttributeCollectionEnum_from_IEnumVARIANT(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);

    if(IsEqualGUID(riid, &IID_IUnknown)) {
        *ppv = &This->IEnumVARIANT_iface;
    }else if(IsEqualGUID(riid, &IID_IEnumVARIANT)) {
        *ppv = &This->IEnumVARIANT_iface;
    }else {
        FIXME("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI HTMLAttributeCollectionEnum_AddRef(IEnumVARIANT *iface)
{
    HTMLAttributeCollectionEnum *This = HTMLAttributeCollectionEnum_from_IEnumVARIANT(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI HTMLAttributeCollectionEnum_Release(IEnumVARIANT *iface)
{
    HTMLAttributeCollectionEnum *This = HTMLAttributeCollectionEnum_from_IEnumVARIANT(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
        IHTMLAttributeCollection_Release(&This->col->IHTMLAttributeCollection_iface);
        free(This);
    }

    return ref;
}

static HRESULT WINAPI HTMLAttributeCollectionEnum_Next(IEnumVARIANT *iface, ULONG celt, VARIANT *rgVar, ULONG *pCeltFetched)
{
    HTMLAttributeCollectionEnum *This = HTMLAttributeCollectionEnum_from_IEnumVARIANT(iface);
    DISPID tmp, dispid = This->iter_dispid;
    HTMLDOMAttribute *attr;
    nsIDOMAttr *nsattr;
    LONG rel_index = 0;
    HRESULT hres;
    ULONG i;

    TRACE("(%p)->(%lu %p %p)\n", This, celt, rgVar, pCeltFetched);

    for(i = 0; i < celt; i++) {
        nsattr = NULL;
        hres = get_attr_dispid_by_relative_idx(This->col, &rel_index, dispid, &tmp, &nsattr);
        if(SUCCEEDED(hres)) {
            dispid = tmp;
            hres = get_domattr(This->col, dispid, nsattr, NULL, &attr);
            if(nsattr)
                nsIDOMAttr_Release(nsattr);
        }
        else if(hres == DISP_E_UNKNOWNNAME)
            break;

        if(FAILED(hres)) {
            while(i--)
                VariantClear(&rgVar[i]);
            return hres;
        }

        V_VT(&rgVar[i]) = VT_DISPATCH;
        V_DISPATCH(&rgVar[i]) = (IDispatch*)&attr->IHTMLDOMAttribute_iface;
    }

    This->iter += i;
    This->iter_dispid = dispid;
    if(pCeltFetched)
        *pCeltFetched = i;
    return i == celt ? S_OK : S_FALSE;
}

static HRESULT WINAPI HTMLAttributeCollectionEnum_Skip(IEnumVARIANT *iface, ULONG celt)
{
    HTMLAttributeCollectionEnum *This = HTMLAttributeCollectionEnum_from_IEnumVARIANT(iface);
    LONG remaining, rel_index;
    DISPID dispid;
    HRESULT hres;

    TRACE("(%p)->(%lu)\n", This, celt);

    if(!celt)
        return S_OK;

    rel_index = -1;
    hres = get_attr_dispid_by_relative_idx(This->col, &rel_index, This->iter_dispid, NULL, NULL);
    if(FAILED(hres))
        return hres;
    remaining = min(celt, rel_index);

    if(remaining) {
        rel_index = remaining - 1;
        hres = get_attr_dispid_by_relative_idx(This->col, &rel_index, This->iter_dispid, &dispid, NULL);
        if(FAILED(hres))
            return hres;
        This->iter += remaining;
        This->iter_dispid = dispid;
    }
    return celt > remaining ? S_FALSE : S_OK;
}

static HRESULT WINAPI HTMLAttributeCollectionEnum_Reset(IEnumVARIANT *iface)
{
    HTMLAttributeCollectionEnum *This = HTMLAttributeCollectionEnum_from_IEnumVARIANT(iface);

    TRACE("(%p)->()\n", This);

    This->iter = 0;
    This->iter_dispid = DISPID_STARTENUM;
    return S_OK;
}

static HRESULT WINAPI HTMLAttributeCollectionEnum_Clone(IEnumVARIANT *iface, IEnumVARIANT **ppEnum)
{
    HTMLAttributeCollectionEnum *This = HTMLAttributeCollectionEnum_from_IEnumVARIANT(iface);
    FIXME("(%p)->(%p)\n", This, ppEnum);
    return E_NOTIMPL;
}

static const IEnumVARIANTVtbl HTMLAttributeCollectionEnumVtbl = {
    HTMLAttributeCollectionEnum_QueryInterface,
    HTMLAttributeCollectionEnum_AddRef,
    HTMLAttributeCollectionEnum_Release,
    HTMLAttributeCollectionEnum_Next,
    HTMLAttributeCollectionEnum_Skip,
    HTMLAttributeCollectionEnum_Reset,
    HTMLAttributeCollectionEnum_Clone
};

/* interface IHTMLAttributeCollection */
static inline HTMLAttributeCollection *impl_from_IHTMLAttributeCollection(IHTMLAttributeCollection *iface)
{
    return CONTAINING_RECORD(iface, HTMLAttributeCollection, IHTMLAttributeCollection_iface);
}

static HRESULT WINAPI HTMLAttributeCollection_QueryInterface(IHTMLAttributeCollection *iface, REFIID riid, void **ppv)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_mshtml_guid(riid), ppv);

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        *ppv = &This->IHTMLAttributeCollection_iface;
    }else if(IsEqualGUID(&IID_IHTMLAttributeCollection, riid)) {
        *ppv = &This->IHTMLAttributeCollection_iface;
    }else if(IsEqualGUID(&IID_IHTMLAttributeCollection2, riid)) {
        *ppv = &This->IHTMLAttributeCollection2_iface;
    }else if(IsEqualGUID(&IID_IHTMLAttributeCollection3, riid)) {
        *ppv = &This->IHTMLAttributeCollection3_iface;
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

static ULONG WINAPI HTMLAttributeCollection_AddRef(IHTMLAttributeCollection *iface)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static ULONG WINAPI HTMLAttributeCollection_Release(IHTMLAttributeCollection *iface)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
        while(!list_empty(&This->attrs)) {
            HTMLDOMAttribute *attr = LIST_ENTRY(list_head(&This->attrs), HTMLDOMAttribute, entry);

            list_remove(&attr->entry);
            attr->elem = NULL;
            IHTMLDOMAttribute_Release(&attr->IHTMLDOMAttribute_iface);
        }

        free(This);
    }

    return ref;
}

static HRESULT WINAPI HTMLAttributeCollection_GetTypeInfoCount(IHTMLAttributeCollection *iface, UINT *pctinfo)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    return IDispatchEx_GetTypeInfoCount(&This->dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLAttributeCollection_GetTypeInfo(IHTMLAttributeCollection *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLAttributeCollection_GetIDsOfNames(IHTMLAttributeCollection *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLAttributeCollection_Invoke(IHTMLAttributeCollection *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
        VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLAttributeCollection_get_length(IHTMLAttributeCollection *iface, LONG *p)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, p);

    *p = -1;
    hres = get_attr_dispid_by_idx(This, p, NULL, NULL);
    return hres;
}

static HRESULT WINAPI HTMLAttributeCollection__newEnum(IHTMLAttributeCollection *iface, IUnknown **p)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    HTMLAttributeCollectionEnum *ret;

    TRACE("(%p)->(%p)\n", This, p);

    ret = malloc(sizeof(*ret));
    if(!ret)
        return E_OUTOFMEMORY;

    ret->IEnumVARIANT_iface.lpVtbl = &HTMLAttributeCollectionEnumVtbl;
    ret->ref = 1;
    ret->iter = 0;
    ret->iter_dispid = DISPID_STARTENUM;

    HTMLAttributeCollection_AddRef(&This->IHTMLAttributeCollection_iface);
    ret->col = This;

    *p = (IUnknown*)&ret->IEnumVARIANT_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLAttributeCollection_item(IHTMLAttributeCollection *iface, VARIANT *name, IDispatch **ppItem)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection(iface);
    nsIDOMAttr *nsattr = NULL;
    HTMLDOMAttribute *attr;
    DISPID id;
    HRESULT hres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_variant(name), ppItem);

    switch(V_VT(name)) {
    case VT_I4:
        hres = get_attr_dispid_by_idx(This, &V_I4(name), &id, &nsattr);
        break;
    case VT_BSTR:
        hres = get_attr_dispid_by_name(This, V_BSTR(name), &id, &nsattr);
        break;
    default:
        FIXME("unsupported name %s\n", debugstr_variant(name));
        hres = E_NOTIMPL;
    }
    if(hres == DISP_E_UNKNOWNNAME)
        return E_INVALIDARG;
    if(FAILED(hres))
        return hres;

    hres = get_domattr(This, id, nsattr, NULL, &attr);
    if(nsattr)
        nsIDOMAttr_Release(nsattr);
    if(FAILED(hres))
        return hres;

    *ppItem = (IDispatch*)&attr->IHTMLDOMAttribute_iface;
    return S_OK;
}

static const IHTMLAttributeCollectionVtbl HTMLAttributeCollectionVtbl = {
    HTMLAttributeCollection_QueryInterface,
    HTMLAttributeCollection_AddRef,
    HTMLAttributeCollection_Release,
    HTMLAttributeCollection_GetTypeInfoCount,
    HTMLAttributeCollection_GetTypeInfo,
    HTMLAttributeCollection_GetIDsOfNames,
    HTMLAttributeCollection_Invoke,
    HTMLAttributeCollection_get_length,
    HTMLAttributeCollection__newEnum,
    HTMLAttributeCollection_item
};

static inline HTMLAttributeCollection *impl_from_IHTMLAttributeCollection2(IHTMLAttributeCollection2 *iface)
{
    return CONTAINING_RECORD(iface, HTMLAttributeCollection, IHTMLAttributeCollection2_iface);
}

static HRESULT WINAPI HTMLAttributeCollection2_QueryInterface(IHTMLAttributeCollection2 *iface, REFIID riid, void **ppv)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    return IHTMLAttributeCollection_QueryInterface(&This->IHTMLAttributeCollection_iface, riid, ppv);
}

static ULONG WINAPI HTMLAttributeCollection2_AddRef(IHTMLAttributeCollection2 *iface)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    return IHTMLAttributeCollection_AddRef(&This->IHTMLAttributeCollection_iface);
}

static ULONG WINAPI HTMLAttributeCollection2_Release(IHTMLAttributeCollection2 *iface)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    return IHTMLAttributeCollection_Release(&This->IHTMLAttributeCollection_iface);
}

static HRESULT WINAPI HTMLAttributeCollection2_GetTypeInfoCount(IHTMLAttributeCollection2 *iface, UINT *pctinfo)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    return IDispatchEx_GetTypeInfoCount(&This->dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLAttributeCollection2_GetTypeInfo(IHTMLAttributeCollection2 *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLAttributeCollection2_GetIDsOfNames(IHTMLAttributeCollection2 *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLAttributeCollection2_Invoke(IHTMLAttributeCollection2 *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
        VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLAttributeCollection2_getNamedItem(IHTMLAttributeCollection2 *iface, BSTR bstrName,
        IHTMLDOMAttribute **newretNode)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    nsIDOMAttr *nsattr = NULL;
    HTMLDOMAttribute *attr;
    DISPID id;
    HRESULT hres;

    TRACE("(%p)->(%s %p)\n", This, debugstr_w(bstrName), newretNode);

    hres = get_attr_dispid_by_name(This, bstrName, &id, &nsattr);
    if(hres == DISP_E_UNKNOWNNAME) {
        *newretNode = NULL;
        return S_OK;
    } else if(FAILED(hres)) {
        return hres;
    }

    hres = get_domattr(This, id, nsattr, NULL, &attr);
    if(nsattr)
        nsIDOMAttr_Release(nsattr);
    if(FAILED(hres))
        return hres;

    *newretNode = &attr->IHTMLDOMAttribute_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLAttributeCollection2_setNamedItem(IHTMLAttributeCollection2 *iface,
        IHTMLDOMAttribute *ppNode, IHTMLDOMAttribute **newretNode)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    FIXME("(%p)->(%p %p)\n", This, ppNode, newretNode);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLAttributeCollection2_removeNamedItem(IHTMLAttributeCollection2 *iface,
        BSTR bstrName, IHTMLDOMAttribute **newretNode)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection2(iface);
    FIXME("(%p)->(%s %p)\n", This, debugstr_w(bstrName), newretNode);
    return E_NOTIMPL;
}

static const IHTMLAttributeCollection2Vtbl HTMLAttributeCollection2Vtbl = {
    HTMLAttributeCollection2_QueryInterface,
    HTMLAttributeCollection2_AddRef,
    HTMLAttributeCollection2_Release,
    HTMLAttributeCollection2_GetTypeInfoCount,
    HTMLAttributeCollection2_GetTypeInfo,
    HTMLAttributeCollection2_GetIDsOfNames,
    HTMLAttributeCollection2_Invoke,
    HTMLAttributeCollection2_getNamedItem,
    HTMLAttributeCollection2_setNamedItem,
    HTMLAttributeCollection2_removeNamedItem
};

static inline HTMLAttributeCollection *impl_from_IHTMLAttributeCollection3(IHTMLAttributeCollection3 *iface)
{
    return CONTAINING_RECORD(iface, HTMLAttributeCollection, IHTMLAttributeCollection3_iface);
}

static HRESULT WINAPI HTMLAttributeCollection3_QueryInterface(IHTMLAttributeCollection3 *iface, REFIID riid, void **ppv)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IHTMLAttributeCollection_QueryInterface(&This->IHTMLAttributeCollection_iface, riid, ppv);
}

static ULONG WINAPI HTMLAttributeCollection3_AddRef(IHTMLAttributeCollection3 *iface)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IHTMLAttributeCollection_AddRef(&This->IHTMLAttributeCollection_iface);
}

static ULONG WINAPI HTMLAttributeCollection3_Release(IHTMLAttributeCollection3 *iface)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IHTMLAttributeCollection_Release(&This->IHTMLAttributeCollection_iface);
}

static HRESULT WINAPI HTMLAttributeCollection3_GetTypeInfoCount(IHTMLAttributeCollection3 *iface, UINT *pctinfo)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IDispatchEx_GetTypeInfoCount(&This->dispex.IDispatchEx_iface, pctinfo);
}

static HRESULT WINAPI HTMLAttributeCollection3_GetTypeInfo(IHTMLAttributeCollection3 *iface, UINT iTInfo,
        LCID lcid, ITypeInfo **ppTInfo)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IDispatchEx_GetTypeInfo(&This->dispex.IDispatchEx_iface, iTInfo, lcid, ppTInfo);
}

static HRESULT WINAPI HTMLAttributeCollection3_GetIDsOfNames(IHTMLAttributeCollection3 *iface, REFIID riid,
        LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IDispatchEx_GetIDsOfNames(&This->dispex.IDispatchEx_iface, riid, rgszNames, cNames,
            lcid, rgDispId);
}

static HRESULT WINAPI HTMLAttributeCollection3_Invoke(IHTMLAttributeCollection3 *iface, DISPID dispIdMember,
        REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
        VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IDispatchEx_Invoke(&This->dispex.IDispatchEx_iface, dispIdMember, riid, lcid,
            wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
}

static HRESULT WINAPI HTMLAttributeCollection3_getNamedItem(IHTMLAttributeCollection3 *iface, BSTR bstrName,
        IHTMLDOMAttribute **ppNodeOut)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IHTMLAttributeCollection2_getNamedItem(&This->IHTMLAttributeCollection2_iface, bstrName, ppNodeOut);
}

static HRESULT WINAPI HTMLAttributeCollection3_setNamedItem(IHTMLAttributeCollection3 *iface,
        IHTMLDOMAttribute *pNodeIn, IHTMLDOMAttribute **ppNodeOut)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    FIXME("(%p)->(%p %p)\n", This, pNodeIn, ppNodeOut);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLAttributeCollection3_removeNamedItem(IHTMLAttributeCollection3 *iface,
        BSTR bstrName, IHTMLDOMAttribute **ppNodeOut)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    FIXME("(%p)->(%s %p)\n", This, debugstr_w(bstrName), ppNodeOut);
    return E_NOTIMPL;
}

static HRESULT WINAPI HTMLAttributeCollection3_item(IHTMLAttributeCollection3 *iface, LONG index, IHTMLDOMAttribute **ppNodeOut)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    nsIDOMAttr *nsattr = NULL;
    HTMLDOMAttribute *attr;
    DISPID id;
    HRESULT hres;

    TRACE("(%p)->(%ld %p)\n", This, index, ppNodeOut);

    hres = get_attr_dispid_by_idx(This, &index, &id, &nsattr);
    if(hres == DISP_E_UNKNOWNNAME)
        return E_INVALIDARG;
    if(FAILED(hres))
        return hres;

    hres = get_domattr(This, id, nsattr, NULL, &attr);
    if(nsattr)
        nsIDOMAttr_Release(nsattr);
    if(FAILED(hres))
        return hres;

    *ppNodeOut = &attr->IHTMLDOMAttribute_iface;
    return S_OK;
}

static HRESULT WINAPI HTMLAttributeCollection3_get_length(IHTMLAttributeCollection3 *iface, LONG *p)
{
    HTMLAttributeCollection *This = impl_from_IHTMLAttributeCollection3(iface);
    return IHTMLAttributeCollection_get_length(&This->IHTMLAttributeCollection_iface, p);
}

static const IHTMLAttributeCollection3Vtbl HTMLAttributeCollection3Vtbl = {
    HTMLAttributeCollection3_QueryInterface,
    HTMLAttributeCollection3_AddRef,
    HTMLAttributeCollection3_Release,
    HTMLAttributeCollection3_GetTypeInfoCount,
    HTMLAttributeCollection3_GetTypeInfo,
    HTMLAttributeCollection3_GetIDsOfNames,
    HTMLAttributeCollection3_Invoke,
    HTMLAttributeCollection3_getNamedItem,
    HTMLAttributeCollection3_setNamedItem,
    HTMLAttributeCollection3_removeNamedItem,
    HTMLAttributeCollection3_item,
    HTMLAttributeCollection3_get_length
};

static inline HTMLAttributeCollection *HTMLAttributeCollection_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, HTMLAttributeCollection, dispex);
}

static HRESULT HTMLAttributeCollection_get_dispid(DispatchEx *dispex, BSTR name, DWORD flags, DISPID *dispid)
{
    HTMLAttributeCollection *This = HTMLAttributeCollection_from_DispatchEx(dispex);
    nsIDOMAttr *nsattr = NULL;
    HTMLDOMAttribute *attr;
    LONG pos;
    HRESULT hres;

    TRACE("(%p)->(%s %lx %p)\n", This, debugstr_w(name), flags, dispid);

    hres = get_attr_dispid_by_name(This, name, dispid, &nsattr);
    if(FAILED(hres))
        return hres;

    hres = get_domattr(This, *dispid, nsattr, &pos, &attr);
    if(nsattr)
        nsIDOMAttr_Release(nsattr);
    if(FAILED(hres))
        return hres;
    IHTMLDOMAttribute_Release(&attr->IHTMLDOMAttribute_iface);

    *dispid = MSHTML_DISPID_CUSTOM_MIN+pos;
    return S_OK;
}

static HRESULT HTMLAttributeCollection_get_name(DispatchEx *dispex, DISPID id, BSTR *name)
{
    HTMLAttributeCollection *This = HTMLAttributeCollection_from_DispatchEx(dispex);

    FIXME("(%p)->(%lx %p)\n", This, id, name);

    return E_NOTIMPL;
}

static HRESULT HTMLAttributeCollection_invoke(DispatchEx *dispex, IDispatch *this_obj, DISPID id, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *res, EXCEPINFO *ei, IServiceProvider *caller)
{
    HTMLAttributeCollection *This = HTMLAttributeCollection_from_DispatchEx(dispex);

    TRACE("(%p)->(%lx %lx %x %p %p %p %p)\n", This, id, lcid, flags, params, res, ei, caller);

    switch(flags) {
    case DISPATCH_PROPERTYGET: {
        HTMLDOMAttribute *iter;
        DWORD pos;

        pos = id-MSHTML_DISPID_CUSTOM_MIN;

        LIST_FOR_EACH_ENTRY(iter, &This->attrs, HTMLDOMAttribute, entry) {
            if(!pos) {
                IHTMLDOMAttribute_AddRef(&iter->IHTMLDOMAttribute_iface);
                V_VT(res) = VT_DISPATCH;
                V_DISPATCH(res) = (IDispatch*)&iter->IHTMLDOMAttribute_iface;
                return S_OK;
            }
            pos--;
        }

        WARN("invalid arg\n");
        return E_INVALIDARG;
    }

    default:
        FIXME("unimplemented flags %x\n", flags);
        return E_NOTIMPL;
    }
}

static const dispex_static_data_vtbl_t HTMLAttributeCollection_dispex_vtbl = {
    NULL,
    HTMLAttributeCollection_get_dispid,
    HTMLAttributeCollection_get_name,
    HTMLAttributeCollection_invoke,
    NULL
};

static const tid_t HTMLAttributeCollection_iface_tids[] = {
    IHTMLAttributeCollection_tid,
    IHTMLAttributeCollection2_tid,
    IHTMLAttributeCollection3_tid,
    0
};

dispex_static_data_t HTMLAttributeCollection_dispex = {
    L"NamedNodeMap",
    &HTMLAttributeCollection_dispex_vtbl,
    PROTO_ID_HTMLAttributeCollection,
    DispHTMLAttributeCollection_tid,
    HTMLAttributeCollection_iface_tids
};

HRESULT HTMLElement_get_attr_col(HTMLDOMNode *iface, HTMLAttributeCollection **ac)
{
    HTMLElement *This = impl_from_HTMLDOMNode(iface);
    compat_mode_t compat_mode = dispex_compat_mode(&iface->event_target.dispex);

    if(This->attrs) {
        IHTMLAttributeCollection_AddRef(&This->attrs->IHTMLAttributeCollection_iface);
        *ac = This->attrs;
        return S_OK;
    }

    This->attrs = calloc(1, sizeof(HTMLAttributeCollection));
    if(!This->attrs)
        return E_OUTOFMEMORY;

    This->attrs->IHTMLAttributeCollection_iface.lpVtbl = &HTMLAttributeCollectionVtbl;
    This->attrs->IHTMLAttributeCollection2_iface.lpVtbl = &HTMLAttributeCollection2Vtbl;
    This->attrs->IHTMLAttributeCollection3_iface.lpVtbl = &HTMLAttributeCollection3Vtbl;
    This->attrs->ref = 2;

    This->attrs->elem = This;
    list_init(&This->attrs->attrs);
    init_dispatch(&This->attrs->dispex, (IUnknown*)&This->attrs->IHTMLAttributeCollection_iface,
                  &HTMLAttributeCollection_dispex, get_inner_window(This->node.doc), compat_mode);

    if(compat_mode >= COMPAT_MODE_IE9 && This->dom_element)
        nsIDOMElement_GetAttributes(This->dom_element, &This->attrs->nsattrs);

    *ac = This->attrs;
    return S_OK;
}
