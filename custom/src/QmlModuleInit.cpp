// QmlModuleInit.cpp
//
// This file exists to ensure QML module type registrations from static libraries
// are linked into the final binary. When QGroundControl is built with static
// libraries, the linker may discard QQmlModuleRegistration globals if nothing
// references them. The actual registration calls live in PreflightPlugin::init()
// (see PreflightPlugin.cpp), which force-links each module's registration function
// to guarantee they survive the link step. This file documents that pattern.
