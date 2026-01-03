/**
 * @file parser.cpp
 * @brief NxXML parser implementation
 * 
 * Minimal XML/HTML parser focused on WebCore needs.
 * HTML mode enables forgiving parsing (auto-close, void elements).
 */

#include "nxxml.h"
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <unordered_set>

// ============================================================================
// Internal Structures
// ============================================================================

struct NxXmlAttr {
    std::string name;
    std::string value;
};

struct NxXmlNode {
    NxXmlNodeType type;
    std::string name;
    std::string value;
    std::vector<NxXmlAttr> attrs;
    NxXmlNode* parent;
    std::vector<NxXmlNode*> children;
};

struct NxXmlDocument {
    NxXmlNode* root;
    std::vector<NxXmlNode*> all_nodes;  // For cleanup
};

// HTML void elements (self-closing)
static const std::unordered_set<std::string> HTML_VOID_ELEMENTS = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr"
};

// ============================================================================
// Parser
// ============================================================================

struct Parser {
    const char* src;
    const char* end;
    const char* pos;
    bool html_mode;
    
    Parser(const char* s, size_t len, bool html) 
        : src(s), end(s + len), pos(s), html_mode(html) {}
    
    char peek() const { return pos < end ? *pos : '\0'; }
    char advance() { return pos < end ? *pos++ : '\0'; }
    void skip(size_t n) { pos += n; if (pos > end) pos = end; }
    
    void skipWhitespace() {
        while (pos < end && isspace(*pos)) pos++;
    }
    
    bool match(const char* str) {
        size_t len = strlen(str);
        if (pos + len > end) return false;
        if (html_mode) {
            for (size_t i = 0; i < len; i++) {
                if (tolower(pos[i]) != tolower(str[i])) return false;
            }
        } else {
            if (memcmp(pos, str, len) != 0) return false;
        }
        pos += len;
        return true;
    }
    
    bool matchNoAdvance(const char* str) {
        size_t len = strlen(str);
        if (pos + len > end) return false;
        return memcmp(pos, str, len) == 0;
    }
    
    std::string readUntil(char c) {
        const char* start = pos;
        while (pos < end && *pos != c) pos++;
        return std::string(start, pos);
    }
    
    std::string readName() {
        const char* start = pos;
        while (pos < end && (isalnum(*pos) || *pos == '-' || *pos == '_' || *pos == ':')) {
            pos++;
        }
        std::string name(start, pos);
        if (html_mode) {
            for (char& c : name) c = tolower(c);
        }
        return name;
    }
    
    std::string readAttrValue() {
        skipWhitespace();
        if (peek() == '"') {
            advance();
            std::string val = readUntil('"');
            if (peek() == '"') advance();
            return val;
        } else if (peek() == '\'') {
            advance();
            std::string val = readUntil('\'');
            if (peek() == '\'') advance();
            return val;
        } else {
            // Unquoted value (HTML mode)
            const char* start = pos;
            while (pos < end && !isspace(*pos) && *pos != '>' && *pos != '/') pos++;
            return std::string(start, pos);
        }
    }
    
    NxXmlNode* parseText() {
        const char* start = pos;
        while (pos < end && *pos != '<') pos++;
        if (pos == start) return nullptr;
        
        std::string text(start, pos);
        
        // Trim and check if empty
        size_t s = 0, e = text.size();
        while (s < e && isspace(text[s])) s++;
        while (e > s && isspace(text[e-1])) e--;
        if (s >= e) return nullptr;
        
        NxXmlNode* node = new NxXmlNode();
        node->type = NX_XML_TEXT;
        node->value = text;
        node->parent = nullptr;
        return node;
    }
    
    NxXmlNode* parseComment() {
        if (!match("<!--")) return nullptr;
        const char* start = pos;
        while (pos + 3 <= end && !matchNoAdvance("-->")) pos++;
        std::string content(start, pos);
        match("-->");
        
        NxXmlNode* node = new NxXmlNode();
        node->type = NX_XML_COMMENT;
        node->value = content;
        node->parent = nullptr;
        return node;
    }
    
    NxXmlNode* parseCData() {
        if (!match("<![CDATA[")) return nullptr;
        const char* start = pos;
        while (pos + 3 <= end && !matchNoAdvance("]]>")) pos++;
        std::string content(start, pos);
        match("]]>");
        
        NxXmlNode* node = new NxXmlNode();
        node->type = NX_XML_CDATA;
        node->value = content;
        node->parent = nullptr;
        return node;
    }
    
    NxXmlNode* parseDoctype() {
        if (!match("<!DOCTYPE") && !match("<!doctype")) return nullptr;
        // Skip until >
        while (pos < end && *pos != '>') pos++;
        if (pos < end) pos++;
        
        NxXmlNode* node = new NxXmlNode();
        node->type = NX_XML_DOCTYPE;
        node->parent = nullptr;
        return node;
    }
    
    NxXmlNode* parseElement() {
        if (peek() != '<') return nullptr;
        advance();
        
        skipWhitespace();
        std::string tagName = readName();
        if (tagName.empty()) return nullptr;
        
        NxXmlNode* node = new NxXmlNode();
        node->type = NX_XML_ELEMENT;
        node->name = tagName;
        node->parent = nullptr;
        
        // Parse attributes
        while (true) {
            skipWhitespace();
            if (peek() == '>' || peek() == '/' || peek() == '\0') break;
            
            std::string attrName = readName();
            if (attrName.empty()) break;
            
            skipWhitespace();
            std::string attrValue;
            if (peek() == '=') {
                advance();
                attrValue = readAttrValue();
            }
            
            node->attrs.push_back({attrName, attrValue});
        }
        
        // Self-closing or void element?
        bool selfClosing = false;
        if (match("/>")) {
            selfClosing = true;
        } else if (match(">")) {
            if (html_mode && HTML_VOID_ELEMENTS.count(tagName)) {
                selfClosing = true;
            }
        }
        
        if (!selfClosing) {
            // Parse children
            while (pos < end) {
                skipWhitespace();
                
                // Check for closing tag
                if (matchNoAdvance("</")) {
                    advance(); advance();
                    std::string closeTag = readName();
                    while (pos < end && *pos != '>') pos++;
                    if (pos < end) pos++;
                    
                    if (html_mode) {
                        // Forgiving: just break, don't require matching tag
                        break;
                    } else {
                        if (closeTag == tagName) break;
                    }
                    break;
                }
                
                NxXmlNode* child = parseNode();
                if (child) {
                    child->parent = node;
                    node->children.push_back(child);
                } else {
                    break;
                }
            }
        }
        
        return node;
    }
    
    NxXmlNode* parseNode() {
        skipWhitespace();
        if (pos >= end) return nullptr;
        
        if (matchNoAdvance("<!--")) return parseComment();
        if (matchNoAdvance("<![CDATA[")) return parseCData();
        if (matchNoAdvance("<!DOCTYPE") || matchNoAdvance("<!doctype")) return parseDoctype();
        if (peek() == '<') return parseElement();
        return parseText();
    }
    
    NxXmlDocument* parseDocument() {
        NxXmlDocument* doc = new NxXmlDocument();
        
        // Create document root
        NxXmlNode* root = new NxXmlNode();
        root->type = NX_XML_DOCUMENT;
        root->parent = nullptr;
        doc->root = root;
        doc->all_nodes.push_back(root);
        
        while (pos < end) {
            NxXmlNode* node = parseNode();
            if (node) {
                node->parent = root;
                root->children.push_back(node);
                collectNodes(doc, node);
            } else {
                break;
            }
        }
        
        return doc;
    }
    
    void collectNodes(NxXmlDocument* doc, NxXmlNode* node) {
        doc->all_nodes.push_back(node);
        for (auto* child : node->children) {
            collectNodes(doc, child);
        }
    }
};

// ============================================================================
// C API Implementation
// ============================================================================

extern "C" {

NxXmlDocument* nx_xml_parse(const char* xml, bool html_mode) {
    if (!xml) return nullptr;
    return nx_xml_parse_len(xml, strlen(xml), html_mode);
}

NxXmlDocument* nx_xml_parse_len(const char* xml, size_t len, bool html_mode) {
    if (!xml) return nullptr;
    Parser parser(xml, len, html_mode);
    return parser.parseDocument();
}

void nx_xml_document_free(NxXmlDocument* doc) {
    if (!doc) return;
    for (auto* node : doc->all_nodes) {
        delete node;
    }
    delete doc;
}

NxXmlNode* nx_xml_document_root(NxXmlDocument* doc) {
    if (!doc || !doc->root) return nullptr;
    // Return first element child (skip document node)
    for (auto* child : doc->root->children) {
        if (child->type == NX_XML_ELEMENT) return child;
    }
    return doc->root;
}

NxXmlNodeType nx_xml_node_type(const NxXmlNode* node) {
    return node ? node->type : NX_XML_DOCUMENT;
}

const char* nx_xml_node_name(const NxXmlNode* node) {
    return node ? node->name.c_str() : "";
}

const char* nx_xml_node_value(const NxXmlNode* node) {
    return node ? node->value.c_str() : "";
}

NxXmlNode* nx_xml_node_parent(const NxXmlNode* node) {
    return node ? node->parent : nullptr;
}

NxXmlNode* nx_xml_node_first_child(const NxXmlNode* node) {
    return (node && !node->children.empty()) ? node->children.front() : nullptr;
}

NxXmlNode* nx_xml_node_last_child(const NxXmlNode* node) {
    return (node && !node->children.empty()) ? node->children.back() : nullptr;
}

NxXmlNode* nx_xml_node_next_sibling(const NxXmlNode* node) {
    if (!node || !node->parent) return nullptr;
    auto& siblings = node->parent->children;
    for (size_t i = 0; i + 1 < siblings.size(); i++) {
        if (siblings[i] == node) return siblings[i + 1];
    }
    return nullptr;
}

NxXmlNode* nx_xml_node_prev_sibling(const NxXmlNode* node) {
    if (!node || !node->parent) return nullptr;
    auto& siblings = node->parent->children;
    for (size_t i = 1; i < siblings.size(); i++) {
        if (siblings[i] == node) return siblings[i - 1];
    }
    return nullptr;
}

size_t nx_xml_node_child_count(const NxXmlNode* node) {
    return node ? node->children.size() : 0;
}

size_t nx_xml_node_attr_count(const NxXmlNode* node) {
    return node ? node->attrs.size() : 0;
}

const char* nx_xml_node_attr_name(const NxXmlNode* node, size_t index) {
    if (!node || index >= node->attrs.size()) return nullptr;
    return node->attrs[index].name.c_str();
}

const char* nx_xml_node_attr_value(const NxXmlNode* node, size_t index) {
    if (!node || index >= node->attrs.size()) return nullptr;
    return node->attrs[index].value.c_str();
}

const char* nx_xml_node_attr(const NxXmlNode* node, const char* name) {
    if (!node || !name) return nullptr;
    for (const auto& attr : node->attrs) {
        if (attr.name == name) return attr.value.c_str();
    }
    return nullptr;
}

bool nx_xml_node_has_attr(const NxXmlNode* node, const char* name) {
    return nx_xml_node_attr(node, name) != nullptr;
}

// Query functions
NxXmlNode* nx_xml_get_element_by_id(NxXmlDocument* doc, const char* id) {
    if (!doc || !id) return nullptr;
    for (auto* node : doc->all_nodes) {
        if (node->type == NX_XML_ELEMENT) {
            const char* nodeId = nx_xml_node_attr(node, "id");
            if (nodeId && strcmp(nodeId, id) == 0) return node;
        }
    }
    return nullptr;
}

NxXmlNode** nx_xml_get_elements_by_tag(NxXmlDocument* doc, const char* tag, size_t* count) {
    if (!doc || !tag || !count) {
        if (count) *count = 0;
        return nullptr;
    }
    
    std::vector<NxXmlNode*> matches;
    for (auto* node : doc->all_nodes) {
        if (node->type == NX_XML_ELEMENT && node->name == tag) {
            matches.push_back(node);
        }
    }
    
    *count = matches.size();
    if (matches.empty()) return nullptr;
    
    NxXmlNode** result = static_cast<NxXmlNode**>(malloc(sizeof(NxXmlNode*) * matches.size()));
    memcpy(result, matches.data(), sizeof(NxXmlNode*) * matches.size());
    return result;
}

static void findElement(NxXmlNode* node, const char* tag, NxXmlNode** result) {
    if (*result) return;
    if (node->type == NX_XML_ELEMENT && node->name == tag) {
        *result = node;
        return;
    }
    for (auto* child : node->children) {
        findElement(child, tag, result);
        if (*result) return;
    }
}

NxXmlNode* nx_xml_find_element(NxXmlNode* root, const char* tag) {
    if (!root || !tag) return nullptr;
    NxXmlNode* result = nullptr;
    findElement(root, tag, &result);
    return result;
}

static void collectText(const NxXmlNode* node, std::string& out) {
    if (node->type == NX_XML_TEXT || node->type == NX_XML_CDATA) {
        out += node->value;
    }
    for (const auto* child : node->children) {
        collectText(child, out);
    }
}

char* nx_xml_inner_text(const NxXmlNode* node) {
    if (!node) return strdup("");
    std::string text;
    collectText(node, text);
    return strdup(text.c_str());
}

static void serializeNode(const NxXmlNode* node, std::string& out, bool pretty, int depth);

static void serializeChildren(const NxXmlNode* node, std::string& out, bool pretty, int depth) {
    for (const auto* child : node->children) {
        serializeNode(child, out, pretty, depth);
    }
}

static void serializeNode(const NxXmlNode* node, std::string& out, bool pretty, int depth) {
    std::string indent = pretty ? std::string(depth * 2, ' ') : "";
    
    switch (node->type) {
        case NX_XML_ELEMENT:
            out += indent + "<" + node->name;
            for (const auto& attr : node->attrs) {
                out += " " + attr.name + "=\"" + attr.value + "\"";
            }
            if (node->children.empty()) {
                out += "/>";
            } else {
                out += ">";
                if (pretty) out += "\n";
                serializeChildren(node, out, pretty, depth + 1);
                if (pretty) out += indent;
                out += "</" + node->name + ">";
            }
            if (pretty) out += "\n";
            break;
            
        case NX_XML_TEXT:
            out += node->value;
            break;
            
        case NX_XML_CDATA:
            out += "<![CDATA[" + node->value + "]]>";
            break;
            
        case NX_XML_COMMENT:
            out += "<!--" + node->value + "-->";
            break;
            
        default:
            serializeChildren(node, out, pretty, depth);
            break;
    }
}

char* nx_xml_inner_html(const NxXmlNode* node) {
    if (!node) return strdup("");
    std::string html;
    serializeChildren(node, html, false, 0);
    return strdup(html.c_str());
}

char* nx_xml_to_string(const NxXmlNode* node, bool pretty) {
    if (!node) return strdup("");
    std::string out;
    serializeNode(node, out, pretty, 0);
    return strdup(out.c_str());
}

} // extern "C"
