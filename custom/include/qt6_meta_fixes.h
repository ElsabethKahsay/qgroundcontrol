#pragma once
#ifndef QGC_CUSTOM_META_FIXES_H
#define QGC_CUSTOM_META_FIXES_H

#include <type_traits>
#include <QtCore/qmetatype.h>

class Vehicle;
class VehicleComponent;

namespace QtPrivate {

template<>
struct is_complete<Vehicle, void> : std::true_type {};

template<>
struct is_complete<VehicleComponent, void> : std::true_type {};

} // namespace QtPrivate

#endif // QGC_CUSTOM_META_FIXES_H
