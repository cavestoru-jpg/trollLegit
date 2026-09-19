// Values are bound at runtime, not at compile time.
//
// Every constant below used to hold a Yarn 1.21.11 intermediary string. The client
// now runs on 1.20 through 26.3, across three namespaces (official, intermediary,
// obfuscated), so the spelling of a symbol is a property of the detected version --
// not of this file. sdk/mappings/mappings.cpp binds every pointer here from the
// generated table once sdk::version has identified the game.
//
// A symbol the running version does not have binds to "" , which is the sentinel
// consumers already gate on (`name[0] != '\0'`). Read the comments below for WHY a
// symbol is the right one; read tools/symbols/symbols.json for what it resolves to.
#ifndef MAPPINGS_HPP
#define MAPPINGS_HPP
#include <memory>
#include <string>
#include <vector>

struct JNIEnv_;
typedef JNIEnv_ JNIEnv;
namespace sdk
{
	namespace mappings
	{
		extern const char* version;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html
		extern const char* minecraftclass_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#instance
		extern const char* minecraftclient_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#instance
		extern const char* minecraftclient_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#player
		extern const char* player_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#player
		extern const char* player_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#world
		extern const char* world_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#world
		extern const char* world_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#crosshairTarget
		extern const char* crosshair_target_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#crosshairTarget
		extern const char* crosshair_target_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#interactionManager
		extern const char* interaction_manager_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#interactionManager
		extern const char* interaction_manager_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#getNetworkHandler()
		extern const char* network_handler_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#getNetworkHandler()
		extern const char* network_handler_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#connection
		extern const char* connection_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#connection
		extern const char* connection_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#doAttack()
		extern const char* do_attack_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#doAttack()
		extern const char* do_attack_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#attackCooldown
		extern const char* attack_cooldown_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#attackCooldown
		extern const char* attack_cooldown_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html
		extern const char* gamerenderer_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#gameRenderer
		extern const char* gamerenderer_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#gameRenderer
		extern const char* gamerenderer_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#updateCrosshairTarget(float)
		extern const char* update_crosshair_target_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#updateCrosshairTarget(float)
		extern const char* update_crosshair_target_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getFov(net.minecraft.client.render.Camera,float,boolean)
		extern const char* get_fov_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getFov(net.minecraft.client.render.Camera,float,boolean)
		extern const char* get_fov_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getCamera()
		extern const char* get_camera_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getCamera()
		extern const char* get_camera_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html
		extern const char* camera_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getYaw()
		extern const char* camera_get_yaw_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getYaw()
		extern const char* camera_get_yaw_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getPitch()
		extern const char* camera_get_pitch_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getPitch()
		extern const char* camera_get_pitch_sig;
		// 1.21.11: Camera.getPos() was removed; read field_18712 (pos) directly via GetObjectField.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#pos
		extern const char* camera_pos_field_name;
		extern const char* camera_pos_field_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html
		extern const char* vec3d_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#x
		extern const char* vec3d_x_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#x
		extern const char* vec3d_x_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#y
		extern const char* vec3d_y_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#y
		extern const char* vec3d_y_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#z
		extern const char* vec3d_z_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#z
		extern const char* vec3d_z_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html
		extern const char* clientplayerentity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#sendMovementPackets()
		extern const char* send_movement_packets_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#sendMovementPackets()
		extern const char* send_movement_packets_sig;
		// 1.21.x: setSprinting(false) only flips the data tracker flag; the
		// STOP_SPRINTING action packet is sent by sendSprintingPacket(), and
		// without it the server won't register the sprint stop before the next
		// attack packet arrives -> no crit.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#sendSprintingPacket()
		extern const char* send_sprinting_packet_name;
		extern const char* send_sprinting_packet_sig;
		// Entity.tick, overridden by ClientPlayerEntity. THIS is the wrapper the
		// rotation rework hooks -- resolve it on class_746 so JNIHook redefines
		// the client player, not a base class shared with every mob.
		//
		// Verified against the bytecode of client-intermediary.jar rather than
		// assumed, after a first attempt hooked the wrong method:
		//
		//   ClientPlayerEntity.tick()      -> sendMovementPackets()   [the look packet]
		//   ClientPlayerEntity.tick()      -> super.tick()
		//     LivingEntity.tick()          -> tickMovement()
		//       LivingEntity.tickMovement()-> travel()  -> ... -> updateVelocity()
		//                                                          [reads getYaw()]
		//
		// So tick() is the smallest scope containing BOTH the movement maths
		// and the packet that reports where we are looking. Wrapping only
		// tickMovement moved the body but still reported the real yaw, which is
		// precisely the mismatch anti-cheats look for.
		// ClientWorld.getEntities() -- every loaded entity, not just the ones in
		// the world's `players` list. Selecting targets from `players` alone
		// silently made the mob and animal filters inert: they could never
		// match, because a mob is not in that list at all.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#getEntities()
		extern const char* client_world_get_entities_name;
		extern const char* client_world_get_entities_sig;
		// MinecraftClient.tick -- the client's whole tick.
		//
		// This is the scope the silent rotation is held across, because every
		// consumer of the angle that matters lives inside it:
		//   GameRenderer.updateCrosshairTarget  -> what a click acts on
		//   ClientPlayerEntity.tick             -> sendMovementPackets, travel
		// The camera is drawn outside it, in the frame, so it keeps the real
		// angle without anything being restored for it.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#tick()
		// The right-click use is hooked too, because PlayerInteractItemC2SPacket
		// carries its OWN yaw and pitch, sampled inside that call rather than
		// taken from the movement packets. A rotation held around the player's
		// tick never reaches it: throw a pearl and the server is told an angle
		// nothing else agreed with. Its names are further down, as
		// interaction_manager_class_sig / interact_item_name / interact_item_sig.
		// --- remaining rotation consumers, each hooked separately ----------
		//
		// Found by disassembling client-intermediary.jar rather than assumed.
		// Each of these samples the player's rotation on its own, outside the
		// wraps around the player's tick, the interaction raycast and the
		// right-click use -- so each needs its own.

		// ClientPlayNetworkHandler. Three places echo a PlayerMoveC2SPacket
		// built from the player's CURRENT angles: the teleport acknowledgement,
		// the reply to PlayerRotationS2CPacket, and the "your vehicle was
		// teleported" branch. They run on the network pump, so every rubber-band
		// or teleport otherwise puts a real-angle look packet on the wire.
		// LivingEntityRenderer.updateRenderState. The model's pitch is baked into
		// the render state here, from entity.getPitch(tickDelta) -- the same
		// getter the camera reads, which is why there is no field that turns the
		// model without also turning the view. Wrapping this one call is what
		// separates them: inside it the entity reports the silent pitch, so the
		// model tilts; the camera's own read happens outside and is untouched.
		//
		// Fires for every living entity, so the callback must identity-check the
		// local player.
		extern const char* living_renderer_class_sig;
		extern const char* update_render_state_name;
		extern const char* update_render_state_sig;

		extern const char* net_handler_class_sig;
		extern const char* on_player_position_look_name;
		extern const char* on_player_position_look_sig;
		extern const char* on_player_rotation_name;
		extern const char* on_player_rotation_sig;
		extern const char* on_entity_position_name;
		extern const char* on_entity_position_sig;

		// TridentItem.onStoppedUsing. The riptide branch launches the player
		// along the look vector it computes here, outside every wrap, so the
		// body flies down the real angle while the server was told another.
		extern const char* trident_item_class_sig;
		extern const char* trident_on_stopped_using_name;
		extern const char* trident_on_stopped_using_sig;

		// AbstractHorseEntity.getControlledRotation. A ridden mount takes its
		// rotation from the rider's real angle during the MOUNT's tick, and
		// that rotation is what VehicleMoveC2SPacket then reports. Camel falls
		// through to this one; the ghast has its own and is not covered here.
		extern const char* horse_class_sig;
		extern const char* controlled_rotation_name;
		extern const char* controlled_rotation_sig;

		extern const char* minecraft_tick_name;
		extern const char* minecraft_tick_sig;
		extern const char* entity_tick_name;
		extern const char* entity_tick_sig;
		// Entity.lastYaw / lastPitch -- the previous-tick angles the renderer
		// interpolates from, NOT the ones sendMovementPackets compares against
		// (those are ClientPlayerEntity.field_3941 / field_3925).
		//
		// Entity.tick() writes these from getYaw()/getPitch(). A hook holding a
		// fake rotation across tick() therefore leaves the fake value in them,
		// and once the real angles are restored the renderer spends the next
		// frames interpolating between fake and real -- the view visibly
		// shakes, with an amplitude equal to how far the two have diverged.
		// Restoring them is what makes the rotation actually silent.
		extern const char* entity_last_yaw_name;
		extern const char* entity_last_yaw_sig;
		extern const char* entity_last_pitch_name;
		extern const char* entity_last_pitch_sig;
		// Same story one level down, for the body and head swing.
		extern const char* living_entity_last_body_yaw_name;
		extern const char* living_entity_last_body_yaw_sig;
		extern const char* living_entity_last_head_yaw_name;
		extern const char* living_entity_last_head_yaw_sig;
		// ClientPlayerEntity.lastRenderYaw / lastRenderPitch, the previous-tick
		// halves of renderYaw (field_3932) / renderPitch (field_3916).
		//
		// ClientPlayerEntity.tickMovementInput (method_66282) drags renderYaw
		// and renderPitch 50% toward the current angles every tick, so a hook
		// holding a fake rotation across the tick leaves them chasing it. They
		// are what the first-person hand is drawn from (HeldItemRenderer
		// class_759.method_22976 lerps lastRender* -> render* and rotates the
		// hand by the difference against the real view angles), which is why
		// leaving them out shows up as the hand shaking at 20 Hz.
		extern const char* last_render_yaw_name;
		extern const char* last_render_yaw_sig;
		extern const char* last_render_pitch_name;
		extern const char* last_render_pitch_sig;
		// LivingEntity.tickMovement, overridden by ClientPlayerEntity. Kept for
		// reference; the hook uses tick() above, which contains it.
		//
		// That the override exists is not guesswork: Mixin cannot @Inject into
		// a method a target class does not declare, and Iris ships
		// MixinLocalPlayer with @Inject(method = "aiStep") -- aiStep being the
		// Mojang name for tickMovement.
		//
		// Hooking here puts us around the whole movement chain
		// (travel -> updateVelocity -> getYaw), which is what lets a yaw swap
		// steer movement without touching the immutable PlayerInput record.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#tickMovement()
		extern const char* tick_movement_name;
		extern const char* tick_movement_sig;
		// Entity.updateVelocity(float speed, Vec3d movementInput) -- the one
		// place the player's yaw is turned into a movement vector:
		//     movementInputToVelocity(movementInput, speed, this.getYaw())
		//
		// Wrapping tickMovement was not enough. It contains sendMovementPackets
		// (so the look packets did carry the fake yaw) but NOT travel, so the
		// physics still ran against the real yaw and the mismatch flagged.
		// This is the same method LiquidBounce corrects, via
		// MixinEntity.moveRelative.
		//
		// Declared on Entity, so the hook redefines class_1297 and fires for
		// every entity -- the callback must identity-check the local player.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#updateVelocity(float,net.minecraft.util.math.Vec3d)
		extern const char* update_velocity_name;
		extern const char* update_velocity_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderYaw
		extern const char* render_yaw_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderYaw
		extern const char* render_yaw_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderPitch
		extern const char* render_pitch_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderPitch
		extern const char* render_pitch_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html
		extern const char* playermovec2spacket_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#yaw
		extern const char* playermovec2spacket_yaw_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#yaw
		extern const char* playermovec2spacket_yaw_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#pitch
		extern const char* playermovec2spacket_pitch_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#pitch
		extern const char* playermovec2spacket_pitch_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#abilities
		extern const char* abilities_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#abilities
		extern const char* abilities_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getEntityInteractionRange()
		extern const char* get_entity_interaction_range_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getEntityInteractionRange()
		extern const char* get_entity_interaction_range_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getAttackCooldownProgress(float)
		extern const char* get_attack_cooldown_progress_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getAttackCooldownProgress(float)
		extern const char* get_attack_cooldown_progress_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html
		extern const char* player_entity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerAbilities.html#flying
		extern const char* fly_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerAbilities.html#flying
		extern const char* fly_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getX()
		extern const char* entity_get_x_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getX()
		extern const char* entity_get_x_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getY()
		extern const char* entity_get_y_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getY()
		extern const char* entity_get_y_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getZ()
		extern const char* entity_get_z_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getZ()
		extern const char* entity_get_z_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getYaw()
		extern const char* entity_get_yaw_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getYaw()
		extern const char* entity_get_yaw_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getPitch()
		extern const char* entity_get_pitch_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getPitch()
		extern const char* entity_get_pitch_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setYaw(float)
		extern const char* entity_set_yaw_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setYaw(float)
		extern const char* entity_set_yaw_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setPitch(float)
		extern const char* entity_set_pitch_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setPitch(float)
		extern const char* entity_set_pitch_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getBoundingBox()
		// The bounding box itself. Was hardcoded as class_238 in the in-world
		// renderer, which is why that renderer refused to start on 26.x and on
		// vanilla -- the intermediary name exists in neither.
		extern const char* aabb_class_sig;

		extern const char* get_bounding_box_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getBoundingBox()
		extern const char* get_bounding_box_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setBoundingBox(net.minecraft.util.math.Box)
		extern const char* set_bounding_box_name;
		// 1.21.11: return type changed from boolean to void.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setBoundingBox(net.minecraft.util.math.Box)
		extern const char* set_bounding_box_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html
		extern const char* entity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setFlag(int,boolean)
		extern const char* entity_set_flag_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setFlag(int,boolean)
		extern const char* entity_set_flag_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minX
		extern const char* box_min_x_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minX
		extern const char* box_min_x_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxX
		extern const char* box_max_x_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxX
		extern const char* box_max_x_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minY
		extern const char* box_min_y_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minY
		extern const char* box_min_y_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxY
		extern const char* box_max_y_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxY
		extern const char* box_max_y_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minZ
		extern const char* box_min_z_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minZ
		extern const char* box_min_z_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxZ
		extern const char* box_max_z_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxZ
		extern const char* box_max_z_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#players
		extern const char* players_field_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#players
		extern const char* players_field_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html
		extern const char* living_entity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#isBlocking()
		extern const char* living_entity_is_blocking_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#isBlocking()
		extern const char* living_entity_is_blocking_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#setSprinting(boolean)
		extern const char* set_sprinting_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#setSprinting(boolean)
		extern const char* set_sprinting_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isSprinting()
		extern const char* is_sprinting_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isSprinting()
		extern const char* is_sprinting_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isOnGround()
		extern const char* is_on_ground_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isOnGround()
		extern const char* is_on_ground_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setVelocity(net.minecraft.util.math.Vec3d)
		extern const char* entity_set_velocity_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setVelocity(net.minecraft.util.math.Vec3d)
		extern const char* entity_set_velocity_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#velocity
		extern const char* entity_velocity_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#velocity
		extern const char* entity_velocity_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#handleFallDamage(double,float,net.minecraft.entity.damage.DamageSource)
		extern const char* entity_handle_fall_damage_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#handleFallDamage(double,float,net.minecraft.entity.damage.DamageSource)
		extern const char* entity_handle_fall_damage_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#fallDistance
		extern const char* entity_fall_distance_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#fallDistance
		extern const char* entity_fall_distance_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#attack(net.minecraft.entity.Entity)
		extern const char* player_attack_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#attack(net.minecraft.entity.Entity)
		extern const char* player_attack_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#attackLivingEntity(net.minecraft.entity.LivingEntity)
		extern const char* player_attack_living_entity_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#attackLivingEntity(net.minecraft.entity.LivingEntity)
		extern const char* player_attack_living_entity_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#inventory
		extern const char* player_inventory_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#inventory
		extern const char* player_inventory_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html
		extern const char* player_inventory_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#selectedSlot
		extern const char* inventory_selected_slot_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#selectedSlot
		extern const char* inventory_selected_slot_sig;
		// 1.21.x: direct SetIntField on selectedSlot no longer propagates to the
		// server (no UpdateSelectedSlotC2SPacket, server keeps thinking we're on
		// the previous slot). Use the new setter instead.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#setSelectedSlot(int)
		extern const char* inventory_set_selected_slot_name;
		extern const char* inventory_set_selected_slot_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#getStack(int)
		extern const char* inventory_get_stack_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#getStack(int)
		extern const char* inventory_get_stack_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#setStack(int,net.minecraft.item.ItemStack)
		extern const char* inventory_set_stack_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#setStack(int,net.minecraft.item.ItemStack)
		extern const char* inventory_set_stack_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#getItem()
		extern const char* itemstack_get_item_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#getItem()
		extern const char* itemstack_get_item_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#isEmpty()
		extern const char* itemstack_is_empty_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#isEmpty()
		extern const char* itemstack_is_empty_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/Item.html
		extern const char* item_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/Item.html#getTranslationKey()
		extern const char* item_get_translation_key_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/Item.html#getTranslationKey()
		extern const char* item_get_translation_key_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerInteractionManager.html#interactItem(net.minecraft.entity.player.PlayerEntity,net.minecraft.util.Hand)
		extern const char* interact_item_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerInteractionManager.html#interactItem(net.minecraft.entity.player.PlayerEntity,net.minecraft.util.Hand)
		extern const char* interact_item_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/Hand.html
		extern const char* hand_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/Hand.html#MAIN_HAND
		extern const char* hand_main_hand_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/Hand.html#MAIN_HAND
		extern const char* hand_main_hand_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/EntityHitResult.html
		extern const char* entity_hit_result_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/EntityHitResult.html#getEntity()
		extern const char* entity_hit_result_get_entity_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/EntityHitResult.html#getEntity()
		extern const char* entity_hit_result_get_entity_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getHealth()
		extern const char* living_entity_get_health_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getHealth()
		extern const char* living_entity_get_health_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getMaxHealth()
		extern const char* living_entity_get_max_health_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getMaxHealth()
		extern const char* living_entity_get_max_health_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#hurtTime
		extern const char* living_entity_hurt_time_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#hurtTime
		extern const char* living_entity_hurt_time_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#offHand
		extern const char* inventory_offhand_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#offHand
		extern const char* inventory_offhand_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html
		extern const char* client_world_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#blockEntities
		extern const char* client_world_block_entities_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#blockEntities
		extern const char* client_world_block_entities_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/BlockEntity.html
		extern const char* block_entity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/BlockEntity.html#getPos()
		extern const char* block_entity_get_pos_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/BlockEntity.html#getPos()
		extern const char* block_entity_get_pos_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html
		extern const char* block_pos_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getX()
		extern const char* block_pos_get_x_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getX()
		extern const char* block_pos_get_x_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getY()
		extern const char* block_pos_get_y_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getY()
		extern const char* block_pos_get_y_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getZ()
		extern const char* block_pos_get_z_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getZ()
		extern const char* block_pos_get_z_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/world/World.html
		extern const char* world_class_sig;
		// getBlockState(BlockPos): BlockState — inherited from BlockView
		// (method_8320). The previous mapping was wrong (had setBlockState's
		// 3-arg sig, so lookups silently failed).
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/world/BlockView.html#getBlockState(net.minecraft.util.math.BlockPos)
		extern const char* world_get_block_state_name;
		extern const char* world_get_block_state_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/AbstractBlock.AbstractBlockState.html
		extern const char* block_state_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/AbstractBlock.AbstractBlockState.html#getBlock()
		extern const char* block_state_get_block_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/AbstractBlock.AbstractBlockState.html#getBlock()
		extern const char* block_state_get_block_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/ChestBlockEntity.html
		extern const char* chest_block_entity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/EnderChestBlockEntity.html
		extern const char* ender_chest_block_entity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/ShulkerBoxBlockEntity.html
		extern const char* shulker_box_block_entity_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/BlockHitResult.html
		extern const char* block_hit_result_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/BlockHitResult.html#getBlockPos()
		extern const char* block_hit_result_get_block_pos_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/BlockHitResult.html#getBlockPos()
		extern const char* block_hit_result_get_block_pos_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/HitResult.html#getType()
		extern const char* block_hit_result_get_type_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/HitResult.html#getType()
		extern const char* block_hit_result_get_type_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/ObsidianBlock.html
		extern const char* obsidian_block_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/RespawnAnchorBlock.html
		extern const char* respawn_anchor_block_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayNetworkHandler.html
		extern const char* network_handler_class_sig;
		// 1.21.x: sendPacket lives on ClientCommonNetworkHandler (superclass of
		// ClientPlayNetworkHandler) as method_52787 and takes a Packet (class_2596),
		// not a String. The old (Ljava/lang/String;)V signature was wrong and
		// silently failed every lookup.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientCommonNetworkHandler.html#sendPacket(net.minecraft.network.packet.Packet)
		extern const char* send_packet_name;
		extern const char* send_packet_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/UpdateSelectedSlotC2SPacket.html
		extern const char* update_selected_slot_c2s_packet_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket.html
		extern const char* player_action_c2s_packet_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html
		extern const char* player_action_c2s_packet_action_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html#SWAP_ITEM_WITH_OFFHAND
		extern const char* swap_item_with_offhand_action_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html#SWAP_ITEM_WITH_OFFHAND
		extern const char* swap_item_with_offhand_action_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/UpdateSelectedSlotC2SPacket.html
		extern const char* pick_from_inventory_c2s_packet_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#ORIGIN
		extern const char* block_pos_origin_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#ORIGIN
		extern const char* block_pos_origin_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Direction.html
		extern const char* direction_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Direction.html#DOWN
		extern const char* direction_down_name;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Direction.html#DOWN
		extern const char* direction_down_sig;
		extern const char* channel_inbound_handler_adapter_class_sig;
		extern const char* channel_read0_name;
		extern const char* channel_read0_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityS2CPacket.html
		extern const char* entity_s2c_packet_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityPositionS2CPacket.html
		extern const char* entity_position_s2c_packet_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityMoveS2CPacket.html
		extern const char* entity_move_s2c_packet_class_sig;
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityTeleportS2CPacket.html
		extern const char* entity_teleport_s2c_packet_class_sig;

		// =====================================================================
		// Killaura additions: silent rotation + movement correction + targeting
		// =====================================================================

		// !!! OBSOLETE ON 1.21.11 — the movement-correction hook these describe
		// cannot work as written, and swapping the names would not fix it.
		//
		// Verified against the official Yarn mappings (tools/verify_mappings.py):
		//   KeyboardInput  class_4152 -> class_743
		//   KeyboardInput.tick(ZF)V   -> gone; Input.tick() is method_3129 ()V
		//   Input.movementForward  field_3905 -> gone
		//   Input.movementSideways field_3907 -> gone
		//   Input.sneaking         field_3909 -> gone
		//
		// Input no longer stores loose forward/sideways floats. It now holds
		// `movementVector` (field_55868, a Vec2f) and `playerInput`
		// (field_54155, a PlayerInput RECORD — immutable, so the old trick of
		// writing the decoded floats back after the original tick has no
		// target). Reworking movement correction means rotating the Vec2f or
		// rebuilding the record, which is a redesign rather than a rename.
		//
		// Left here, correct-as-of-this-build, so the rework has a starting
		// point. Nothing reads them yet.
		extern const char* keyboard_input_class_sig;
		extern const char* input_tick_name;
		extern const char* input_tick_sig;
		extern const char* input_class_sig;
		extern const char* input_movement_vector_name;
		extern const char* input_movement_vector_sig;
		extern const char* input_player_input_name;
		extern const char* input_player_input_sig;
		extern const char* player_input_class_sig;

		// Kept as empty strings so movement_correction_hook still compiles and
		// takes its existing "accessor missing -> refuse to attach" path,
		// instead of pretending to work with names that no longer exist.
		extern const char* keyboard_input_tick_name;
		extern const char* keyboard_input_tick_sig;
		extern const char* input_forward_name;
		extern const char* input_forward_sig;
		extern const char* input_sideways_name;
		extern const char* input_sideways_sig;
		// ClientPlayerEntity.input
		extern const char* client_player_input_field_name;
		extern const char* client_player_input_field_sig;

		// MobEntity / AnimalEntity for type filtering.
		extern const char* mob_entity_class_sig;
		extern const char* animal_entity_class_sig;

		// Entity.isAlive()
		extern const char* entity_is_alive_name;
		extern const char* entity_is_alive_sig;

		// =====================================================================
		// Everything below was read out of the official Yarn v2 mapping file
		// (tools/mappings, queried with tools/yarn_lookup.py) rather than
		// guessed or inferred from a JVMTI dump. The client targets exactly one
		// Minecraft version, so the mapping table is authoritative here.
		//
		// It also settled an earlier dispute: Entity.lastRenderX really is
		// field_6038, not the field_6014 triple that sits before `pos`.
		// =====================================================================

		// LivingEntity.getAbsorptionAmount() — added to both current and max HP
		// so a golden apple reads as 30/30 rather than a green 20/20.
		extern const char* living_entity_get_absorption_name;
		extern const char* living_entity_get_absorption_sig;

		// ScoreHolder.getNameForScoreboard() -> String, implemented by Entity.
		// The identity servers key teams and friends on, as opposed to the
		// decorated display name that may carry rank prefixes and colours.
		extern const char* entity_scoreboard_name_name;
		extern const char* entity_scoreboard_name_sig;

		// Entity.getScoreboardTeam() -> Team
		extern const char* entity_get_team_name;
		extern const char* entity_get_team_sig;

		// Entity.getHeight() -> float, for the name-tag anchor.
		extern const char* entity_get_height_name;
		extern const char* entity_get_height_sig;

		// ---- Scoreboard / teams: hiding Minecraft's own floating names ----
		// The technique is the reference's: force every visible player's team to
		// nameTagVisibility = NEVER, parking teamless players in a throwaway
		// team, and put it all back on the way out.
		extern const char* world_get_scoreboard_name;
		extern const char* world_get_scoreboard_sig;
		extern const char* scoreboard_class_sig;

		// Scoreboard.getScoreHolderTeam(String) -> Team
		extern const char* scoreboard_get_holder_team_name;
		extern const char* scoreboard_get_holder_team_sig;
		// Scoreboard.getTeam(String) -> Team
		extern const char* scoreboard_get_team_name;
		extern const char* scoreboard_get_team_sig;
		// Scoreboard.addTeam(String) -> Team
		extern const char* scoreboard_add_team_name;
		extern const char* scoreboard_add_team_sig;
		// Scoreboard.addScoreHolderToTeam(String, Team) -> boolean
		extern const char* scoreboard_add_holder_to_team_name;
		extern const char* scoreboard_add_holder_to_team_sig;
		// Scoreboard.clearTeam(String) -> boolean  (removes a holder from its team)
		extern const char* scoreboard_clear_team_name;
		extern const char* scoreboard_clear_team_sig;

		extern const char* team_class_sig;
		extern const char* abstract_team_class_sig;
		// AbstractTeam.getName() -> String
		extern const char* team_get_name_name;
		extern const char* team_get_name_sig;
		// AbstractTeam.getNameTagVisibilityRule() -> VisibilityRule
		extern const char* team_get_visibility_name;
		extern const char* team_get_visibility_sig;
		// Team.setNameTagVisibilityRule(VisibilityRule)
		extern const char* team_set_visibility_name;
		extern const char* team_set_visibility_sig;
		// Team.getPrefix() -> Text
		extern const char* team_get_prefix_name;
		extern const char* team_get_prefix_sig;

		extern const char* visibility_class_sig;
		extern const char* visibility_never_name;
		extern const char* visibility_sig;

		// LivingEntity.isUsingItem() — for NoAttackWhenEat.
		extern const char* living_entity_is_using_item_name;
		extern const char* living_entity_is_using_item_sig;

		// Entity.getName() -> Text — used for friends matching.
		extern const char* entity_get_name_name;
		extern const char* entity_get_name_sig;
		extern const char* text_class_sig;
		// Not yarn-mangled — Text.getString() comes from the interface.
		extern const char* text_get_string_name;
		extern const char* text_get_string_sig;

		// LivingEntity body/head yaw — kept in sync with fake yaw inside the
		// silent rotation hook, otherwise other players see the head/body
		// glued to the real yaw and the killaura is visual-detectable.
		extern const char* living_entity_body_yaw_name;
		extern const char* living_entity_body_yaw_sig;
		extern const char* living_entity_head_yaw_name;
		extern const char* living_entity_head_yaw_sig;

		// BlockItem — used by the autoclicker's RMB "blocks only" gate to
		// confirm the player is currently holding a placeable block (and
		// not eg. food / a tool / a sword).
		extern const char* block_item_class_sig;

		// Input.sneaking — write-target for the Eagle module. Setting this
		// to true on the local player's input is the same path the vanilla
		// keyboard input takes when you hold LShift; MC then drives all the
		// usual side effects (movement clamp at edges, pose, packet) for us.
		// Obsolete with the rest of the Input refactor; Eagle drives sneak
		// through the KeyBinding path below instead, which still resolves.
		extern const char* input_sneaking_name;
		extern const char* input_sneaking_sig;

		// BlockState.isAir() — used by Eagle's edge detector.
		extern const char* block_state_is_air_name;
		extern const char* block_state_is_air_sig;

		// ---- Teams module: armor / equipment access ----
		// LivingEntity.getEquippedStack(EquipmentSlot)
		extern const char* living_entity_get_equipped_stack_name;
		extern const char* living_entity_get_equipped_stack_sig;
		// EquipmentSlot enum
		extern const char* equipment_slot_class_sig;
		extern const char* equipment_slot_sig;
		extern const char* equipment_slot_head_name;  // HEAD
		extern const char* equipment_slot_chest_name;  // CHEST
		extern const char* equipment_slot_legs_name;  // LEGS
		extern const char* equipment_slot_feet_name;  // FEET
		// Dye colour moved out of LeatherArmorItem entirely: class_1738 and its
		// getColor(ItemStack) are both gone on 1.21.11. The colour now lives in
		// a data component, read through a STATIC helper:
		//   DyedColorComponent.getColor(ItemStack, int fallback) -> int
		// Verified against the official Yarn mappings.
		extern const char* dyed_color_component_class_sig;
		extern const char* dyed_color_get_color_name;
		extern const char* dyed_color_get_color_sig;

		// ---- Eagle: KeyBinding override path ----
		// MinecraftClient.options : GameOptions, GameOptions.sneakKey :
		// KeyBinding, KeyBinding.setPressed(boolean). Writing here goes
		// straight into the field that Input.tick() reads as the sneak
		// state — no GLFW round-trip, no fighting KeyboardInput.tick.
		extern const char* mc_options_field_name;
		extern const char* mc_options_field_sig;
		extern const char* gameoptions_class_sig;
		extern const char* gameoptions_sneak_key_name;
		extern const char* gameoptions_sneak_key_sig;
		// GameOptions.sprintKey. Same route as sneak, and for the same reason:
		// LivingEntity.setSprinting mutates the entity's attribute-modifier map
		// (the sprint speed boost), and that map is not thread-safe. Calling it
		// from the client's worker thread races the tick thread doing the same
		// inside tickMovement and corrupts the map — observed as
		// ArrayIndexOutOfBoundsException(-1) inside Object2ObjectArrayMap.remove.
		// Pressing the key instead lets Minecraft flip sprinting on its own
		// thread, where the mutation is serialised with everything else.
		extern const char* gameoptions_sprint_key_name;
		extern const char* gameoptions_sprint_key_sig;
		extern const char* keybinding_class_sig;
		extern const char* keybinding_set_pressed_name;
		extern const char* keybinding_set_pressed_sig;

		// =====================================================================
		// In-world renderer: hook target + per-frame interpolation
		//
		// Taken from a JVMTI dump of the running game (sdk::java::
		// dump_render_mappings), not guessed — a wrong obfuscated name resolves
		// to a null id and fails silently at runtime, which is far harder to
		// diagnose than a missing mapping.
		// =====================================================================

		// WorldRenderer.render. The parameter list of the native hook must match
		// this signature exactly, because the hook forwards the arguments to
		// the original: (ObjectAllocator, RenderTickCounter, boolean, Camera,
		// Matrix4f, Matrix4f, Matrix4f, GpuBufferSlice, Vector4f, boolean).
		extern const char* world_renderer_class_sig;
		extern const char* world_renderer_render_name;
		extern const char* world_renderer_render_sig;

		// The submit-node path, 1.21.9 and later.
		//
		// From 1.21.9 the frame no longer takes geometry immediately: it is queued
		// into LevelRenderer's own SubmitNodeStorage and drained by phase. That is
		// the route the in-world ESP takes, because everything below sits ABOVE the
		// backend split -- blaze3d.{opengl,vulkan} on 26.2, renderpearl.backend.*
		// on 26.3 -- so the game dispatches our geometry to whichever backend is
		// live and this client never names OpenGL or Vulkan at all.
		//
		// Reaching the storage needs no hook: it is a field. The hook is only for
		// WHEN, and it goes on submitEntities -- which neither Sodium nor Iris
		// touches, unlike renderLevel above.
		extern const char* submit_node_storage_class_sig;
		extern const char* submit_node_collector_class_sig;
		extern const char* ordered_submit_collector_class_sig;
		extern const char* custom_geometry_renderer_class_sig;

		extern const char* level_renderer_submit_node_storage_name;
		extern const char* level_renderer_submit_node_storage_sig;
		extern const char* level_renderer_submit_entities_name;
		extern const char* level_renderer_submit_entities_sig;
		extern const char* submit_node_order_name;
		extern const char* submit_node_order_sig;
		extern const char* submit_custom_geometry_name;
		extern const char* submit_custom_geometry_sig;

		// The callback the proxy implements: render(PoseStack$Pose, VertexConsumer).
		// Byte-identical on every version that has it, which is what lets one
		// implementation cover the whole branch.
		extern const char* custom_geometry_render_name;
		extern const char* custom_geometry_render_sig;

		// Which bucket the geometry is drawn in. Taken from the factories rather
		// than the LINES / DEBUG_FILLED_BOX statics: before 1.21.11 those fields
		// are declared RenderType$CompositeRenderType, so their descriptor does not
		// survive the package split, while the factories' return type does.
		extern const char* render_types_class_sig;
		extern const char* render_type_lines_name;
		extern const char* render_type_lines_sig;
		extern const char* render_type_debug_filled_box_name;
		extern const char* render_type_debug_filled_box_sig;

		// Which primitive a render type draws. The geometry is a triangle LIST, and
		// DEBUG_FILLED_BOX is a triangle STRIP -- feeding one to the other turns box
		// faces into uneven halves with sides missing a triangle. DEBUG_QUADS reads
		// four vertices at a time and a triangle becomes an exact quad by repeating
		// its last vertex. The value is an enum, so name() comes from the JDK.
		extern const char* render_type_topology_name;
		extern const char* render_type_topology_sig;
		extern const char* render_type_debug_quads_name;
		extern const char* render_type_debug_quads_sig;

		// Name tags drawn by the game: its font, its background, its phase, and
		// whichever backend it is running on. Read off EntityRenderer.submitNameDisplay,
		// which calls it as (pose, nameTagAttachment, 0, nameTag, !isDiscrete,
		// lightCoords, camera) with the pose already translated to the entity.
		extern const char* submit_name_tag_name;
		extern const char* submit_name_tag_sig;
		extern const char* level_render_state_class_sig;
		extern const char* level_render_state_camera_name;
		extern const char* level_render_state_camera_sig;
		extern const char* vec3_zero_name;
		extern const char* vec3_zero_sig;
		extern const char* component_null_to_empty_name;
		extern const char* component_null_to_empty_sig;
		extern const char* pose_stack_class_sig;
		extern const char* pose_stack_push_name;
		extern const char* pose_stack_push_sig;
		extern const char* pose_stack_pop_name;
		extern const char* pose_stack_pop_sig;
		extern const char* pose_stack_translate_name;
		extern const char* pose_stack_translate_sig;

		// The game's own vertex sink, and the transform handed to the callback
		// beside it. addVertex takes the pose overload so the geometry lands in the
		// same space as everything else submitted that frame; setNormal is only
		// needed for lines, whose render type reads the normal as the segment
		// direction and uses it to give the line its width.
		extern const char* vertex_consumer_class_sig;
		extern const char* pose_stack_pose_class_sig;
		extern const char* vertex_add_vertex_name;
		extern const char* vertex_add_vertex_sig;
		extern const char* vertex_set_color_name;
		extern const char* vertex_set_color_sig;
		extern const char* vertex_set_normal_name;
		extern const char* vertex_set_normal_sig;

		// What a render type wants per vertex. Asked rather than assumed: a vertex
		// short of an element makes BufferBuilder.build() throw from inside the
		// game's own drain, after our callback has returned, where nothing here can
		// catch it. Element names are Position, Color, Normal, UV0..UV3.
		extern const char* render_type_format_name;
		extern const char* render_type_format_sig;
		extern const char* vertex_format_get_elements_name;
		extern const char* vertex_format_get_elements_sig;
		extern const char* vertex_format_element_name_name;
		extern const char* vertex_format_element_name_sig;

		extern const char* vertex_set_line_width_name;
		extern const char* vertex_set_line_width_sig;
		extern const char* vertex_set_uv_name;
		extern const char* vertex_set_uv_sig;
		extern const char* vertex_set_uv1_name;
		extern const char* vertex_set_uv1_sig;
		extern const char* vertex_set_uv2_name;
		extern const char* vertex_set_uv2_sig;

		// Entity.lastRenderX / lastRenderY / lastRenderZ — the positions the
		// game interpolates from when it draws a frame between two ticks.
		// Without these the boxes sit on the raw tick position and step at
		// 20 Hz while the entity model moves smoothly.
		//
		// Pinned by declaration order in the JVMTI dump against three fields
		// this file already maps: field_18276 (velocity), field_6005
		// (boundingBox) and field_6017 (fallDistance). Those land exactly where
		// Yarn's Entity puts them, which fixes the rest of the layout:
		//
		//   world -> field_6014/6036/5969 -> pos -> ... -> velocity
		//        so that first triple is prevX/prevY/prevZ, NOT lastRender
		//   ... -> fallDistance -> nextStepSoundDistance -> field_6038/5971/5989
		//        which is lastRenderX/Y/Z, the pair the renderer lerps from
		//
		// The two triples usually hold identical values (both are written at
		// tick start), so reading the wrong one looks correct until something
		// updates only one of them.
		extern const char* entity_last_render_x_name;
		extern const char* entity_last_render_y_name;
		extern const char* entity_last_render_z_name;
		extern const char* entity_last_render_sig;

		// MinecraftClient.renderTickCounter — the field is typed as the concrete
		// RenderTickCounter.Dynamic, while getTickProgress lives on the base.
		extern const char* mc_render_tick_counter_name;
		extern const char* mc_render_tick_counter_sig;
		extern const char* render_tick_counter_class_sig;
		// RenderTickCounter.getTickProgress.
		//
		// This used to be deliberately empty, on the theory that the renderer
		// would scan class_9779 for its only (Z)F method instead of carrying
		// another obfuscated name. No such scan was ever written: the code fed
		// the empty string straight to GetMethodID, which cannot match anything,
		// so tick progress resolved to null on every single run and the boxes
		// silently stepped at 20 Hz — "interpolation mappings absent" in the log.
		//
		// Naming it is the safer half of the fix: tools/verify_mappings.py
		// checks every intermediary name in this header against the official
		// Yarn file, so a rename is caught at build time rather than becoming
		// another silent null. The scan now exists too, as a fallback.
		extern const char* render_tick_counter_progress_name;
		extern const char* render_tick_counter_progress_sig;

		// There is deliberately no render-method mapping here any more.
		//
		// The in-world renderer used to hook one, and every candidate failed on
		// a modded install: WorldRenderer.render is rewritten by Sodium and
		// Iris, InGameHud.render by fabric-rendering-v1, and JNIHook replaces
		// the body of whatever it hooks. It now subscribes to Fabric's
		// WorldRenderEvents instead and hooks nothing — see the notes at the
		// top of world_render_hook.cpp.

		// ClientPlayerInteractionManager.attackEntity(PlayerEntity, Entity)
		// — fired DIRECTLY by the killaura instead of going through the
		// crosshair-based MinecraftClient.doAttack(). Avoids the race with
		// updateCrosshairTarget and lets us call from the JVM tick thread
		// right after sendMovementPackets (ensures Look(fake) → Attack
		// packet ordering, otherwise Grim's PacketOrder check fires).
		extern const char* attack_entity_name;
		extern const char* attack_entity_sig;

		// LivingEntity.swingHand(Hand) — plays the arm-swing animation AND sends
		// the HandSwingC2SPacket. Vanilla doAttack() calls this right after
		// attackEntity(); attackEntity alone deals the hit but never swings, so
		// without it there is no animation and no swing packet.
		extern const char* swing_hand_name;
		extern const char* swing_hand_sig;
		// Hand enum + Hand.MAIN_HAND reuse the existing hand_class_sig /
		// hand_main_hand_name / hand_main_hand_sig defined above.

		// LivingEntityRenderer.render, the pre-1.21.2 home of the model's pitch.
		// Absent from 1.21.2 on, where updateRenderState carries it instead --
		// deliberately, so the two hooks can never both be installed.
		extern const char* living_renderer_render_name;
		extern const char* living_renderer_render_sig;

		// Entity.getXRot() -- the pitch as it stands. entity_get_pitch is the
		// (F)F tick-delta variant, which is a different question and the reason
		// the rotation hooks used to name this one by hand.
		extern const char* entity_get_pitch_noarg_name;
		extern const char* entity_get_pitch_noarg_sig;

		// Dye colour before 1.20.5: an interface method on the item, not the
		// static component helper that replaced it.
		extern const char* dyeable_item_class_sig;
		extern const char* dyeable_get_color_name;
		extern const char* dyeable_get_color_sig;

		// MultiPlayerGameMode.getPickRange -- the interaction distance before
		// 1.20.5 introduced the entity_interaction_range attribute. Returns a
		// float where the attribute getter returns a double.
		extern const char* pick_range_name;
		extern const char* pick_range_sig;

		// Storage ESP matches a block state's block against these parent
		// classes. They used to be hardcoded intermediary names, which meant the
		// module could only ever work under Fabric.
		extern const char* abstract_chest_block_class_sig;
		extern const char* barrel_block_class_sig;
		extern const char* shulker_box_block_class_sig;
		extern const char* hopper_block_class_sig;
		extern const char* dispenser_block_class_sig;
		extern const char* crafter_block_class_sig;
		extern const char* abstract_furnace_block_class_sig;

		// BlockPos extends Vec3i, and its coordinates are read through these.
		extern const char* vec3i_get_x_name;
		extern const char* vec3i_get_x_sig;
		extern const char* vec3i_get_y_name;
		extern const char* vec3i_get_y_sig;
		extern const char* vec3i_get_z_name;
		extern const char* vec3i_get_z_sig;

		// Vec2, the movement vector ClientInput has held since 1.21.5. Writing
		// input means building one of these; before 1.21.5 the same two numbers
		// are mutable float fields and this is absent.
		extern const char* vec2_class_sig;
		extern const char* vec2_x_name;
		extern const char* vec2_x_sig;
		extern const char* vec2_y_name;
		extern const char* vec2_y_sig;

		// 26.3 turned swings into an item component: swing(Hand) became
		// swing(Hand, SwingAnimation, boolean). SwingAnimation.DEFAULT is the
		// stock animation, which is what an attack with no item-specific swing
		// should carry. Absent on every earlier version, where the two-argument
		// form does not exist -- sdk::compat::swing_hand picks the shape from the
		// descriptor rather than from a version number.
		extern const char* swing_animation_class_sig;
		extern const char* swing_animation_default_name;
		extern const char* swing_animation_default_sig;

		// --- binding ------------------------------------------------------

		// Identifies the namespace the JVM is using and points every constant
		// above at the right string for the detected version. Must run after
		// sdk::classloader::init and before any symbol is looked up; calling it
		// twice is a no-op. Returns false when nothing resolved at all.
		bool bind(JNIEnv* env);

		bool bound();

		// True when a symbol resolved on this version. Every consumer already
		// treats "" as absent, so this is the same test spelled once:
		//     if (!sdk::mappings::have(sdk::mappings::update_render_state_name))
		inline bool have(const char* value) { return value && value[0] != '\0'; }

		// The class a symbol lives on, in the active namespace.
		//
		// Members move between classes across versions -- GameRenderer.pick became
		// Minecraft.pick in 26.1 -- so a hook that pairs a method constant with a
		// separately chosen class constant quietly targets the wrong class. Ask the
		// table instead:
		//
		//     const char* owner = sdk::mappings::owner_of("update_crosshair_target");
		//
		// Returns "" when the symbol is absent on this version.
		const char* owner_of(const char* symbol_id);

		// Diagnostics for the menu and the log: which symbols this version does
		// not have, by spec id (e.g. "update_render_state").
		int symbol_count();
		const std::vector<const char*>& unresolved();
	}
};

#endif // MAPPINGS_HPP