use eframe::egui;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::Mutex;
use crate::config::Config;
use crate::languages::LanguageManager;

pub struct ReoLabIDE {
    config: Config,
    language_manager: LanguageManager,
    current_file: Option<PathBuf>,
    project_path: Option<PathBuf>,
    editor_content: String,
    terminal_output: String,
    show_project_explorer: bool,
    show_terminal: bool,
}

impl ReoLabIDE {
    pub fn new(config: Config) -> Self {
        Self {
            language_manager: LanguageManager::new(),
            current_file: None,
            project_path: None,
            editor_content: String::new(),
            terminal_output: String::new(),
            show_project_explorer: true,
            show_terminal: true,
        }
    }
}

impl eframe::App for ReoLabIDE {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            // Menu bar
            self.render_menu_bar(ui);
            
            // Main layout
            egui::TopBottomPanel::top("toolbar").show(ctx, |ui| {
                self.render_toolbar(ui);
            });
            
            // Main content area
            egui::CentralPanel::default().show(ctx, |ui| {
                self.render_main_content(ui);
            });
            
            // Status bar
            egui::TopBottomPanel::bottom("status_bar").show(ctx, |ui| {
                self.render_status_bar(ui);
            });
        });
    }
}

impl ReoLabIDE {
    fn render_menu_bar(&mut self, ui: &mut egui::Ui) {
        egui::menu::bar(ui, |ui| {
            ui.menu_button("File", |ui| {
                if ui.button("New File").clicked() {
                    self.new_file();
                }
                if ui.button("Open File").clicked() {
                    self.open_file();
                }
                if ui.button("Save").clicked() {
                    self.save_file();
                }
                if ui.button("Save As").clicked() {
                    self.save_file_as();
                }
                ui.separator();
                if ui.button("Exit").clicked() {
                    ui.ctx().send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });
            
            ui.menu_button("Edit", |ui| {
                if ui.button("Undo").clicked() {
                    // TODO: Implement undo
                }
                if ui.button("Redo").clicked() {
                    // TODO: Implement redo
                }
                ui.separator();
                if ui.button("Find").clicked() {
                    // TODO: Implement find
                }
                if ui.button("Replace").clicked() {
                    // TODO: Implement replace
                }
            });
            
            ui.menu_button("Build", |ui| {
                if ui.button("Build").clicked() {
                    self.build_project();
                }
                if ui.button("Run").clicked() {
                    self.run_project();
                }
                if ui.button("Clean").clicked() {
                    self.clean_project();
                }
            });
            
            ui.menu_button("View", |ui| {
                ui.checkbox(&mut self.show_project_explorer, "Project Explorer");
                ui.checkbox(&mut self.show_terminal, "Terminal");
            });
            
            ui.menu_button("Help", |ui| {
                if ui.button("About").clicked() {
                    self.show_about();
                }
            });
        });
    }
    
    fn render_toolbar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            if ui.button("🆕 New").clicked() {
                self.new_file();
            }
            if ui.button("📁 Open").clicked() {
                self.open_file();
            }
            if ui.button("💾 Save").clicked() {
                self.save_file();
            }
            ui.separator();
            if ui.button("🔨 Build").clicked() {
                self.build_project();
            }
            if ui.button("▶️ Run").clicked() {
                self.run_project();
            }
            if ui.button("🛑 Stop").clicked() {
                self.stop_project();
            }
        });
    }
    
    fn render_main_content(&mut self, ui: &mut egui::Ui) {
        egui::SidePanel::left("project_explorer")
            .resizable(true)
            .default_width(200.0)
            .show_cond(ui.ctx(), self.show_project_explorer, |ui| {
                self.render_project_explorer(ui);
            });
        
        egui::CentralPanel::default().show(ui, |ui| {
            self.render_editor(ui);
        });
        
        egui::TopBottomPanel::bottom("terminal")
            .resizable(true)
            .default_height(150.0)
            .show_cond(ui.ctx(), self.show_terminal, |ui| {
                self.render_terminal(ui);
            });
    }
    
    fn render_project_explorer(&mut self, ui: &mut egui::Ui) {
        ui.heading("Project Explorer");
        ui.separator();
        
        if let Some(project_path) = &self.project_path {
            self.render_file_tree(ui, project_path);
        } else {
            ui.label("No project opened");
            if ui.button("Open Project").clicked() {
                self.open_project();
            }
        }
    }
    
    fn render_editor(&mut self, ui: &mut egui::Ui) {
        ui.heading("Editor");
        
        let mut text = self.editor_content.clone();
        let text_edit = egui::TextEdit::multiline(&mut text)
            .desired_width(f32::INFINITY)
            .desired_rows(20)
            .font(egui::TextStyle::Monospace);
        
        if ui.add(text_edit).changed() {
            self.editor_content = text;
        }
    }
    
    fn render_terminal(&mut self, ui: &mut egui::Ui) {
        ui.heading("Terminal");
        ui.separator();
        
        egui::ScrollArea::vertical().show(ui, |ui| {
            ui.label(&self.terminal_output);
        });
        
        ui.horizontal(|ui| {
            let mut command = String::new();
            let response = ui.text_edit_singleline(&mut command);
            
            if response.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
                self.execute_terminal_command(&command);
            }
        });
    }
    
    fn render_status_bar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            if let Some(file) = &self.current_file {
                ui.label(format!("File: {}", file.display()));
            } else {
                ui.label("No file open");
            }
            
            ui.separator();
            
            ui.label(format!("Line: {}", 1)); // TODO: Get actual line number
            ui.label(format!("Column: {}", 1)); // TODO: Get actual column number
            
            ui.separator();
            
            ui.label("Ready");
        });
    }
    
    fn render_file_tree(&mut self, ui: &mut egui::Ui, path: &PathBuf) {
        // Simple file tree implementation
        if let Ok(entries) = std::fs::read_dir(path) {
            for entry in entries {
                if let Ok(entry) = entry {
                    let name = entry.file_name().to_string_lossy().to_string();
                    let is_dir = entry.file_type().map(|ft| ft.is_dir()).unwrap_or(false);
                    
                    let icon = if is_dir { "📁" } else { "📄" };
                    if ui.button(format!("{} {}", icon, name)).clicked() {
                        if !is_dir {
                            self.open_file_at_path(entry.path());
                        }
                    }
                }
            }
        }
    }
    
    // Action methods
    fn new_file(&mut self) {
        self.current_file = None;
        self.editor_content.clear();
    }
    
    fn open_file(&mut self) {
        // TODO: Implement file dialog
        self.terminal_output.push_str("Opening file...\n");
    }
    
    fn save_file(&mut self) {
        // TODO: Implement save
        self.terminal_output.push_str("Saving file...\n");
    }
    
    fn save_file_as(&mut self) {
        // TODO: Implement save as
        self.terminal_output.push_str("Save as...\n");
    }
    
    fn open_project(&mut self) {
        // TODO: Implement project opening
        self.terminal_output.push_str("Opening project...\n");
    }
    
    fn open_file_at_path(&mut self, path: PathBuf) {
        if let Ok(content) = std::fs::read_to_string(&path) {
            self.current_file = Some(path);
            self.editor_content = content;
        }
    }
    
    fn build_project(&mut self) {
        self.terminal_output.push_str("Building project...\n");
        // TODO: Implement actual build
    }
    
    fn run_project(&mut self) {
        self.terminal_output.push_str("Running project...\n");
        // TODO: Implement actual run
    }
    
    fn stop_project(&mut self) {
        self.terminal_output.push_str("Stopping project...\n");
        // TODO: Implement actual stop
    }
    
    fn clean_project(&mut self) {
        self.terminal_output.push_str("Cleaning project...\n");
        // TODO: Implement actual clean
    }
    
    fn execute_terminal_command(&mut self, command: &str) {
        self.terminal_output.push_str(&format!("$ {}\n", command));
        // TODO: Implement actual command execution
        self.terminal_output.push_str("Command executed\n");
    }
    
    fn show_about(&mut self) {
        // TODO: Implement about dialog
        self.terminal_output.push_str("ReoLab IDE v0.1.0\n");
    }
} 