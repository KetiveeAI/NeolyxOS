/**
 * @file nxxml.h
 * @brief NxXML - Minimal XML/HTML parser for WebCore
 * 
 * Lightweight parser with only what WebCore needs:
 * - DOM tree building
 * - Element traversal
 * - Attribute access
 * - Text content
 */

#ifndef NXXML_H
#define NXXML_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Error Codes
// ============================================================================

typedef enum {
    NX_XML_OK = 0,
    NX_XML_ERROR = -1,
    NX_XML_ERROR_NOMEM = -2,
    NX_XML_ERROR_SYNTAX = -3,
    NX_XML_ERROR_UNCLOSED_TAG = -4,
    NX_XML_ERROR_UNEXPECTED_CLOSE = -5
} NxXmlError;

// ============================================================================
// Node Types
// ============================================================================

typedef enum {
    NX_XML_ELEMENT = 1,
    NX_XML_TEXT = 3,
    NX_XML_CDATA = 4,
    NX_XML_COMMENT = 8,
    NX_XML_DOCUMENT = 9,
    NX_XML_DOCTYPE = 10
} NxXmlNodeType;

// ============================================================================
// Forward Declarations
// ============================================================================

typedef struct NxXmlNode NxXmlNode;
typedef struct NxXmlDocument NxXmlDocument;

// ============================================================================
// Document
// ============================================================================

/**
 * Parse XML/HTML string into document tree.
 * @param xml Source string (null-terminated)
 * @param html_mode If true, forgiving HTML parsing (auto-close, case-insensitive)
 * @return Document or NULL on error
 */
NxXmlDocument* nx_xml_parse(const char* xml, bool html_mode);
NxXmlDocument* nx_xml_parse_len(const char* xml, size_t len, bool html_mode);

void nx_xml_document_free(NxXmlDocument* doc);
NxXmlNode* nx_xml_document_root(NxXmlDocument* doc);

// ============================================================================
// Node Access
// ============================================================================

NxXmlNodeType nx_xml_node_type(const NxXmlNode* node);
const char* nx_xml_node_name(const NxXmlNode* node);      // Tag name for elements
const char* nx_xml_node_value(const NxXmlNode* node);     // Text content

// Tree navigation
NxXmlNode* nx_xml_node_parent(const NxXmlNode* node);
NxXmlNode* nx_xml_node_first_child(const NxXmlNode* node);
NxXmlNode* nx_xml_node_last_child(const NxXmlNode* node);
NxXmlNode* nx_xml_node_next_sibling(const NxXmlNode* node);
NxXmlNode* nx_xml_node_prev_sibling(const NxXmlNode* node);

// Child count
size_t nx_xml_node_child_count(const NxXmlNode* node);

// ============================================================================
// Attributes
// ============================================================================

size_t nx_xml_node_attr_count(const NxXmlNode* node);
const char* nx_xml_node_attr_name(const NxXmlNode* node, size_t index);
const char* nx_xml_node_attr_value(const NxXmlNode* node, size_t index);
const char* nx_xml_node_attr(const NxXmlNode* node, const char* name);
bool nx_xml_node_has_attr(const NxXmlNode* node, const char* name);

// ============================================================================
// Query
// ============================================================================

/**
 * Find element by ID.
 */
NxXmlNode* nx_xml_get_element_by_id(NxXmlDocument* doc, const char* id);

/**
 * Find elements by tag name. Returns array, caller must free.
 */
NxXmlNode** nx_xml_get_elements_by_tag(NxXmlDocument* doc, const char* tag, size_t* count);

/**
 * Find first element matching tag name.
 */
NxXmlNode* nx_xml_find_element(NxXmlNode* root, const char* tag);

/**
 * Get inner text (all descendant text content).
 */
char* nx_xml_inner_text(const NxXmlNode* node);

/**
 * Get inner HTML/XML.
 */
char* nx_xml_inner_html(const NxXmlNode* node);

// ============================================================================
// Serialization
// ============================================================================

char* nx_xml_to_string(const NxXmlNode* node, bool pretty);

#ifdef __cplusplus
}
#endif

// ============================================================================
// C++ Wrapper
// ============================================================================

#ifdef __cplusplus
#include <string>
#include <vector>
#include <memory>

namespace nx {

class XmlNode {
public:
    XmlNode(NxXmlNode* node) : node_(node) {}
    
    NxXmlNodeType type() const { return nx_xml_node_type(node_); }
    std::string name() const { return nx_xml_node_name(node_) ?: ""; }
    std::string value() const { return nx_xml_node_value(node_) ?: ""; }
    
    XmlNode parent() const { return XmlNode(nx_xml_node_parent(node_)); }
    XmlNode firstChild() const { return XmlNode(nx_xml_node_first_child(node_)); }
    XmlNode nextSibling() const { return XmlNode(nx_xml_node_next_sibling(node_)); }
    
    std::string attr(const std::string& name) const {
        const char* v = nx_xml_node_attr(node_, name.c_str());
        return v ? v : "";
    }
    
    bool hasAttr(const std::string& name) const {
        return nx_xml_node_has_attr(node_, name.c_str());
    }
    
    std::string innerText() const {
        char* text = nx_xml_inner_text(node_);
        std::string result(text ? text : "");
        free(text);
        return result;
    }
    
    bool valid() const { return node_ != nullptr; }
    explicit operator bool() const { return valid(); }
    
    // Iteration
    std::vector<XmlNode> children() const {
        std::vector<XmlNode> result;
        for (auto child = firstChild(); child; child = child.nextSibling()) {
            result.push_back(child);
        }
        return result;
    }
    
private:
    NxXmlNode* node_;
};

class XmlDocument {
public:
    XmlDocument() : doc_(nullptr) {}
    ~XmlDocument() { if (doc_) nx_xml_document_free(doc_); }
    
    XmlDocument(XmlDocument&& other) noexcept : doc_(other.doc_) { other.doc_ = nullptr; }
    XmlDocument& operator=(XmlDocument&& other) noexcept {
        if (doc_) nx_xml_document_free(doc_);
        doc_ = other.doc_;
        other.doc_ = nullptr;
        return *this;
    }
    
    XmlDocument(const XmlDocument&) = delete;
    XmlDocument& operator=(const XmlDocument&) = delete;
    
    static XmlDocument parse(const std::string& xml, bool htmlMode = false) {
        XmlDocument doc;
        doc.doc_ = nx_xml_parse(xml.c_str(), htmlMode);
        return doc;
    }
    
    static XmlDocument parseHtml(const std::string& html) {
        return parse(html, true);
    }
    
    XmlNode root() const { return XmlNode(nx_xml_document_root(doc_)); }
    bool valid() const { return doc_ != nullptr; }
    
    XmlNode getElementById(const std::string& id) const {
        return XmlNode(nx_xml_get_element_by_id(doc_, id.c_str()));
    }
    
    XmlNode findElement(const std::string& tag) const {
        return XmlNode(nx_xml_find_element(nx_xml_document_root(doc_), tag.c_str()));
    }
    
private:
    NxXmlDocument* doc_;
};

} // namespace nx

#endif // __cplusplus

#endif // NXXML_H
