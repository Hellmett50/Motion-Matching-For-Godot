//
//   █████▒█    ██  ▄████▄   ██ ▄█▀       ██████╗ ██╗   ██╗ ██████╗
// ▓██   ▒ ██  ▓██▒▒██▀ ▀█   ██▄█▒        ██╔══██╗██║   ██║██╔════╝
// ▒████ ░▓██  ▒██░▒▓█    ▄ ▓███▄░        ██████╔╝██║   ██║██║  ███╗
// ░▓█▒  ░▓▓█  ░██░▒▓▓▄ ▄██▒▓██ █▄        ██╔══██╗██║   ██║██║   ██║
// ░▒█░   ▒▒█████▓ ▒ ▓███▀ ░▒██▒ █▄       ██████╔╝╚██████╔╝╚██████╔╝
//  ▒ ░   ░▒▓▒ ▒ ▒ ░ ░▒ ▒  ░▒ ▒▒ ▓▒       ╚═════╝  ╚═════╝  ╚═════╝
//  ░     ░░▒░ ░ ░   ░  ▒   ░ ░▒ ▒░
//  ░ ░    ░░░ ░ ░ ░        ░ ░░ ░
//           ░     ░ ░      ░  ░
//   
#include "register_types.h"
#include "core/object/class_db.h"
#include "motion_matching.h"
#include "core/config/engine.h"
#include "mm_spring_damper.h"
#include "editor/plugins/animation_blend_tree_editor_plugin.h"

static MMSpringDamper* _mm_spring_damper = nullptr;

void initialize_motion_matching_module(ModuleInitializationLevel p_level) {
	// register and initialization
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	ClassDB::register_class<MotionMatching>();
    ClassDB::register_class<MMSpringDamper>();
    
    _mm_spring_damper = memnew(MMSpringDamper);

    Engine::get_singleton()->add_singleton(Engine::Singleton("MMSpringDamper", MMSpringDamper::get_singleton()));
}

void uninitialize_motion_matching_module(ModuleInitializationLevel p_level) {
	// Clean up
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	//memdelete(_debug_draw_3d);
    memdelete(_mm_spring_damper);
    //memdelete(_animation_solver);
}