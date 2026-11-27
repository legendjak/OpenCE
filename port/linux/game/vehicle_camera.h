/*
VEHICLE_CAMERA.H

The view getting into and out of a seat, drawn around the player's own body
(vehicle_camera.c).
*/

#ifndef __VEHICLE_CAMERA_H
#define __VEHICLE_CAMERA_H
#pragma once

/* ---------- prototypes/VEHICLE_CAMERA.C */

/* the view of local player `local_player_index` drawn this frame, from the
one the frame would draw (render_interpolation_camera's) */
struct observer_result const *vehicle_camera_view(
	short local_player_index,
	struct observer_result const *camera);
/* whether the object of `object_index` is the body of local player
`local_player_index`, which the view drawn is inside of while it moves to or
from first person: not drawn, nor what it carries (render_objects.c) */
boolean vehicle_camera_hides(
	short local_player_index,
	long object_index);

#endif // __VEHICLE_CAMERA_H
