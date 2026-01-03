// REOX Compiler - Type System Definitions
// Zero external dependencies

use std::collections::HashMap;

/// Resolved type (after type checking)
#[derive(Debug, Clone, PartialEq)]
pub enum ResolvedType {
    Int,
    Float,
    String,
    Bool,
    Void,
    Struct(String),
    Array(Box<ResolvedType>),
    Function {
        params: Vec<ResolvedType>,
        ret: Box<ResolvedType>,
    },
    Unknown,
    Error,
}

impl ResolvedType {
    /// Convert parser Type to ResolvedType
    pub fn from_parser_type(ty: &crate::parser::Type) -> Self {
        match ty {
            crate::parser::Type::Int => ResolvedType::Int,
            crate::parser::Type::Float => ResolvedType::Float,
            crate::parser::Type::String => ResolvedType::String,
            crate::parser::Type::Bool => ResolvedType::Bool,
            crate::parser::Type::Void => ResolvedType::Void,
            crate::parser::Type::Named(name) => ResolvedType::Struct(name.clone()),
            crate::parser::Type::Array(inner) => {
                ResolvedType::Array(Box::new(Self::from_parser_type(inner)))
            }
        }
    }

    /// Check if types are compatible for assignment
    pub fn is_assignable_from(&self, other: &ResolvedType) -> bool {
        if *self == *other {
            return true;
        }
        // Int can be assigned from Int
        // Float can be assigned from Int (widening)
        matches!(
            (self, other),
            (ResolvedType::Float, ResolvedType::Int)
        )
    }

    /// Get display name for error messages
    pub fn display_name(&self) -> String {
        match self {
            ResolvedType::Int => "int".to_string(),
            ResolvedType::Float => "float".to_string(),
            ResolvedType::String => "string".to_string(),
            ResolvedType::Bool => "bool".to_string(),
            ResolvedType::Void => "void".to_string(),
            ResolvedType::Struct(name) => name.clone(),
            ResolvedType::Array(inner) => format!("[{}]", inner.display_name()),
            ResolvedType::Function { params, ret } => {
                let params_str: Vec<String> = params.iter().map(|p| p.display_name()).collect();
                format!("fn({}) -> {}", params_str.join(", "), ret.display_name())
            }
            ResolvedType::Unknown => "<unknown>".to_string(),
            ResolvedType::Error => "<error>".to_string(),
        }
    }
}

/// Symbol entry in symbol table
#[derive(Debug, Clone)]
pub struct Symbol {
    pub name: String,
    pub ty: ResolvedType,
    pub mutable: bool,
    pub kind: SymbolKind,
}

/// Kind of symbol
#[derive(Debug, Clone, PartialEq)]
pub enum SymbolKind {
    Variable,
    Function,
    Struct,
    Parameter,
}

/// Struct definition info
#[derive(Debug, Clone)]
pub struct StructInfo {
    pub name: String,
    pub fields: HashMap<String, ResolvedType>,
}

/// Scope in the symbol table
#[derive(Debug)]
struct Scope {
    symbols: HashMap<String, Symbol>,
}

impl Scope {
    fn new() -> Self {
        Self {
            symbols: HashMap::new(),
        }
    }
}

/// Symbol table for scope management
#[derive(Debug)]
pub struct SymbolTable {
    scopes: Vec<Scope>,
    structs: HashMap<String, StructInfo>,
    functions: HashMap<String, ResolvedType>,
}

impl SymbolTable {
    pub fn new() -> Self {
        Self {
            scopes: vec![Scope::new()], // Global scope
            structs: HashMap::new(),
            functions: HashMap::new(),
        }
    }

    /// Enter a new scope
    pub fn push_scope(&mut self) {
        self.scopes.push(Scope::new());
    }

    /// Exit current scope
    pub fn pop_scope(&mut self) {
        if self.scopes.len() > 1 {
            self.scopes.pop();
        }
    }

    /// Define a symbol in current scope
    pub fn define(&mut self, symbol: Symbol) -> Result<(), String> {
        let scope = self.scopes.last_mut().expect("no scope");
        if scope.symbols.contains_key(&symbol.name) {
            return Err(format!(
                "symbol '{}' already defined in this scope",
                symbol.name
            ));
        }
        scope.symbols.insert(symbol.name.clone(), symbol);
        Ok(())
    }

    /// Look up a symbol by name (searches all scopes)
    pub fn lookup(&self, name: &str) -> Option<&Symbol> {
        for scope in self.scopes.iter().rev() {
            if let Some(sym) = scope.symbols.get(name) {
                return Some(sym);
            }
        }
        None
    }

    /// Define a struct
    pub fn define_struct(&mut self, info: StructInfo) -> Result<(), String> {
        if self.structs.contains_key(&info.name) {
            return Err(format!("struct '{}' already defined", info.name));
        }
        self.structs.insert(info.name.clone(), info);
        Ok(())
    }

    /// Look up a struct
    pub fn lookup_struct(&self, name: &str) -> Option<&StructInfo> {
        self.structs.get(name)
    }

    /// Define a function
    pub fn define_function(&mut self, name: String, ty: ResolvedType) -> Result<(), String> {
        if self.functions.contains_key(&name) {
            return Err(format!("function '{}' already defined", name));
        }
        self.functions.insert(name, ty);
        Ok(())
    }

    /// Look up a function
    pub fn lookup_function(&self, name: &str) -> Option<&ResolvedType> {
        self.functions.get(name)
    }
}

impl Default for SymbolTable {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_symbol_table_basic() {
        let mut table = SymbolTable::new();
        
        table.define(Symbol {
            name: "x".to_string(),
            ty: ResolvedType::Int,
            mutable: false,
            kind: SymbolKind::Variable,
        }).unwrap();

        assert!(table.lookup("x").is_some());
        assert!(table.lookup("y").is_none());
    }

    #[test]
    fn test_symbol_table_scopes() {
        let mut table = SymbolTable::new();
        
        table.define(Symbol {
            name: "x".to_string(),
            ty: ResolvedType::Int,
            mutable: false,
            kind: SymbolKind::Variable,
        }).unwrap();

        table.push_scope();
        
        table.define(Symbol {
            name: "y".to_string(),
            ty: ResolvedType::Int,
            mutable: false,
            kind: SymbolKind::Variable,
        }).unwrap();

        // Can see both x and y
        assert!(table.lookup("x").is_some());
        assert!(table.lookup("y").is_some());

        table.pop_scope();

        // Can only see x now
        assert!(table.lookup("x").is_some());
        assert!(table.lookup("y").is_none());
    }

    #[test]
    fn test_type_assignability() {
        assert!(ResolvedType::Int.is_assignable_from(&ResolvedType::Int));
        assert!(ResolvedType::Float.is_assignable_from(&ResolvedType::Int));
        assert!(!ResolvedType::Int.is_assignable_from(&ResolvedType::String));
    }
}
