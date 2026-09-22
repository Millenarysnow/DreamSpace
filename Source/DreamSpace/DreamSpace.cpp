// DreamSpace 主模块入口。
#include "DreamSpace.h"
#include "Modules/ModuleManager.h"

// 主模块统一日志分类的定义；旧的 LogDreamInteraction 已随策划可配置交互框架一并移除。
DEFINE_LOG_CATEGORY(LogDreamSpace);

IMPLEMENT_PRIMARY_GAME_MODULE( FDefaultGameModuleImpl, DreamSpace, "DreamSpace" );
