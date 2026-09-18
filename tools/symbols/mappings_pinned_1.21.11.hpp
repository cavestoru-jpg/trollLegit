#ifndef MAPPINGS_HPP
#define MAPPINGS_HPP
#include <memory>

#include <string>
namespace sdk
{
	namespace mappings
	{
		static const char* version = "1.21.11";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html
		static const char* minecraftclass_sig = "net/minecraft/class_310";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#instance
		static const char* minecraftclient_name = "field_1700";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#instance
		static const char* minecraftclient_sig = "Lnet/minecraft/class_310;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#player
		static const char* player_name = "field_1724";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#player
		static const char* player_sig = "Lnet/minecraft/class_746;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#world
		static const char* world_name = "field_1687";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#world
		static const char* world_sig = "Lnet/minecraft/class_638;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#crosshairTarget
		static const char* crosshair_target_name = "field_1765";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#crosshairTarget
		static const char* crosshair_target_sig = "Lnet/minecraft/class_239;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#interactionManager
		static const char* interaction_manager_name = "field_1761";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#interactionManager
		static const char* interaction_manager_sig = "Lnet/minecraft/class_636;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#getNetworkHandler()
		static const char* network_handler_name = "method_1562";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#getNetworkHandler()
		static const char* network_handler_sig = "()Lnet/minecraft/class_634;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#connection
		static const char* connection_name = "field_1746";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#connection
		static const char* connection_sig = "Lnet/minecraft/class_2535;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#doAttack()
		static const char* do_attack_name = "method_1536";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#doAttack()
		static const char* do_attack_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#attackCooldown
		static const char* attack_cooldown_name = "field_1771";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#attackCooldown
		static const char* attack_cooldown_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html
		static const char* gamerenderer_class_sig = "net/minecraft/class_757";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#gameRenderer
		static const char* gamerenderer_name = "field_1773";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/MinecraftClient.html#gameRenderer
		static const char* gamerenderer_sig = "Lnet/minecraft/class_757;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#updateCrosshairTarget(float)
		static const char* update_crosshair_target_name = "method_3190";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#updateCrosshairTarget(float)
		static const char* update_crosshair_target_sig = "(F)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getFov(net.minecraft.client.render.Camera,float,boolean)
		static const char* get_fov_name = "method_3196";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getFov(net.minecraft.client.render.Camera,float,boolean)
		static const char* get_fov_sig = "(Lnet/minecraft/class_4184;FZ)F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getCamera()
		static const char* get_camera_name = "method_19418";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/GameRenderer.html#getCamera()
		static const char* get_camera_sig = "()Lnet/minecraft/class_4184;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html
		static const char* camera_class_sig = "net/minecraft/class_4184";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getYaw()
		static const char* camera_get_yaw_name = "method_19330";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getYaw()
		static const char* camera_get_yaw_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getPitch()
		static const char* camera_get_pitch_name = "method_19329";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#getPitch()
		static const char* camera_get_pitch_sig = "()F";
		// 1.21.11: Camera.getPos() was removed; read field_18712 (pos) directly via GetObjectField.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/render/Camera.html#pos
		static const char* camera_pos_field_name = "field_18712";
		static const char* camera_pos_field_sig = "Lnet/minecraft/class_243;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html
		static const char* vec3d_class_sig = "net/minecraft/class_243";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#x
		static const char* vec3d_x_name = "field_1352";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#x
		static const char* vec3d_x_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#y
		static const char* vec3d_y_name = "field_1351";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#y
		static const char* vec3d_y_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#z
		static const char* vec3d_z_name = "field_1350";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Vec3d.html#z
		static const char* vec3d_z_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html
		static const char* clientplayerentity_class_sig = "net/minecraft/class_746";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#sendMovementPackets()
		static const char* send_movement_packets_name = "method_3136";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#sendMovementPackets()
		static const char* send_movement_packets_sig = "()V";
		// 1.21.x: setSprinting(false) only flips the data tracker flag; the
		// STOP_SPRINTING action packet is sent by sendSprintingPacket(), and
		// without it the server won't register the sprint stop before the next
		// attack packet arrives -> no crit.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#sendSprintingPacket()
		static const char* send_sprinting_packet_name = "method_46742";
		static const char* send_sprinting_packet_sig  = "()V";
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
		static const char* client_world_get_entities_name = "method_18112";
		static const char* client_world_get_entities_sig  = "()Ljava/lang/Iterable;";
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
		static const char* living_renderer_class_sig = "net/minecraft/class_922";
		static const char* update_render_state_name = "method_62355";
		static const char* update_render_state_sig  =
			"(Lnet/minecraft/class_1309;Lnet/minecraft/class_10042;F)V";

		static const char* net_handler_class_sig = "net/minecraft/class_634";
		static const char* on_player_position_look_name = "method_11157";
		static const char* on_player_position_look_sig  = "(Lnet/minecraft/class_2708;)V";
		static const char* on_player_rotation_name = "method_64554";
		static const char* on_player_rotation_sig  = "(Lnet/minecraft/class_10265;)V";
		static const char* on_entity_position_name = "method_11086";
		static const char* on_entity_position_sig  = "(Lnet/minecraft/class_2777;)V";

		// TridentItem.onStoppedUsing. The riptide branch launches the player
		// along the look vector it computes here, outside every wrap, so the
		// body flies down the real angle while the server was told another.
		static const char* trident_item_class_sig = "net/minecraft/class_1835";
		static const char* trident_on_stopped_using_name = "method_7840";
		static const char* trident_on_stopped_using_sig  =
			"(Lnet/minecraft/class_1799;Lnet/minecraft/class_1937;Lnet/minecraft/class_1309;I)Z";

		// AbstractHorseEntity.getControlledRotation. A ridden mount takes its
		// rotation from the rider's real angle during the MOUNT's tick, and
		// that rotation is what VehicleMoveC2SPacket then reports. Camel falls
		// through to this one; the ghast has its own and is not covered here.
		static const char* horse_class_sig = "net/minecraft/class_1496";
		static const char* controlled_rotation_name = "method_49489";
		static const char* controlled_rotation_sig  =
			"(Lnet/minecraft/class_1309;)Lnet/minecraft/class_241;";

		static const char* minecraft_tick_name = "method_1574";
		static const char* minecraft_tick_sig  = "()V";
		static const char* entity_tick_name = "method_5773";
		static const char* entity_tick_sig  = "()V";
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
		static const char* entity_last_yaw_name = "field_5982";
		static const char* entity_last_yaw_sig  = "F";
		static const char* entity_last_pitch_name = "field_6004";
		static const char* entity_last_pitch_sig  = "F";
		// Same story one level down, for the body and head swing.
		static const char* living_entity_last_body_yaw_name = "field_6220";
		static const char* living_entity_last_body_yaw_sig  = "F";
		static const char* living_entity_last_head_yaw_name = "field_6259";
		static const char* living_entity_last_head_yaw_sig  = "F";
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
		static const char* last_render_yaw_name = "field_3931";
		static const char* last_render_yaw_sig  = "F";
		static const char* last_render_pitch_name = "field_3914";
		static const char* last_render_pitch_sig  = "F";
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
		static const char* tick_movement_name = "method_6007";
		static const char* tick_movement_sig  = "()V";
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
		static const char* update_velocity_name = "method_5724";
		static const char* update_velocity_sig  = "(FLnet/minecraft/class_243;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderYaw
		static const char* render_yaw_name = "field_3932";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderYaw
		static const char* render_yaw_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderPitch
		static const char* render_pitch_name = "field_3916";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerEntity.html#renderPitch
		static const char* render_pitch_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html
		static const char* playermovec2spacket_class_sig = "net/minecraft/class_2828";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#yaw
		static const char* playermovec2spacket_yaw_name = "field_12887";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#yaw
		static const char* playermovec2spacket_yaw_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#pitch
		static const char* playermovec2spacket_pitch_name = "field_12885";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket.html#pitch
		static const char* playermovec2spacket_pitch_sig = "F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#abilities
		static const char* abilities_name = "field_7503";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#abilities
		static const char* abilities_sig = "Lnet/minecraft/class_1656;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getEntityInteractionRange()
		static const char* get_entity_interaction_range_name = "method_55755";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getEntityInteractionRange()
		static const char* get_entity_interaction_range_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getAttackCooldownProgress(float)
		static const char* get_attack_cooldown_progress_name = "method_7261";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#getAttackCooldownProgress(float)
		static const char* get_attack_cooldown_progress_sig = "(F)F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html
		static const char* player_entity_class_sig = "net/minecraft/class_1657";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerAbilities.html#flying
		static const char* fly_name = "field_7479";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerAbilities.html#flying
		static const char* fly_sig = "Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getX()
		static const char* entity_get_x_name = "method_23317";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getX()
		static const char* entity_get_x_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getY()
		static const char* entity_get_y_name = "method_23318";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getY()
		static const char* entity_get_y_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getZ()
		static const char* entity_get_z_name = "method_23321";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getZ()
		static const char* entity_get_z_sig = "()D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getYaw()
		static const char* entity_get_yaw_name = "method_36454";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getYaw()
		static const char* entity_get_yaw_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getPitch()
		static const char* entity_get_pitch_name = "method_5695";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getPitch()
		static const char* entity_get_pitch_sig = "(F)F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setYaw(float)
		static const char* entity_set_yaw_name = "method_36456";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setYaw(float)
		static const char* entity_set_yaw_sig = "(F)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setPitch(float)
		static const char* entity_set_pitch_name = "method_36457";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setPitch(float)
		static const char* entity_set_pitch_sig = "(F)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getBoundingBox()
		static const char* get_bounding_box_name = "field_6005";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#getBoundingBox()
		static const char* get_bounding_box_sig = "Lnet/minecraft/class_238;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setBoundingBox(net.minecraft.util.math.Box)
		static const char* set_bounding_box_name = "method_5857";
		// 1.21.11: return type changed from boolean to void.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setBoundingBox(net.minecraft.util.math.Box)
		static const char* set_bounding_box_sig = "(Lnet/minecraft/class_238;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html
		static const char* entity_class_sig = "net/minecraft/class_1297";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setFlag(int,boolean)
		static const char* entity_set_flag_name = "method_5729";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setFlag(int,boolean)
		static const char* entity_set_flag_sig = "(IZ)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minX
		static const char* box_min_x_name = "field_1323";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minX
		static const char* box_min_x_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxX
		static const char* box_max_x_name = "field_1320";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxX
		static const char* box_max_x_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minY
		static const char* box_min_y_name = "field_1322";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minY
		static const char* box_min_y_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxY
		static const char* box_max_y_name = "field_1325";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxY
		static const char* box_max_y_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minZ
		static const char* box_min_z_name = "field_1321";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#minZ
		static const char* box_min_z_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxZ
		static const char* box_max_z_name = "field_1324";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Box.html#maxZ
		static const char* box_max_z_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#players
		static const char* players_field_name = "field_18226";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#players
		static const char* players_field_sig = "Ljava/util/List;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html
		static const char* living_entity_class_sig = "net/minecraft/class_1309";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#isBlocking()
		static const char* living_entity_is_blocking_name = "method_6039";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#isBlocking()
		static const char* living_entity_is_blocking_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#setSprinting(boolean)
		static const char* set_sprinting_name = "method_5728";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#setSprinting(boolean)
		static const char* set_sprinting_sig = "(Z)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isSprinting()
		static const char* is_sprinting_name = "method_5624";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isSprinting()
		static const char* is_sprinting_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isOnGround()
		static const char* is_on_ground_name = "method_24828";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#isOnGround()
		static const char* is_on_ground_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setVelocity(net.minecraft.util.math.Vec3d)
		static const char* entity_set_velocity_name = "method_18800";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#setVelocity(net.minecraft.util.math.Vec3d)
		static const char* entity_set_velocity_sig = "(DDD)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#velocity
		static const char* entity_velocity_name = "field_18276";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#velocity
		static const char* entity_velocity_sig = "Lnet/minecraft/class_243;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#handleFallDamage(double,float,net.minecraft.entity.damage.DamageSource)
		static const char* entity_handle_fall_damage_name = "method_5747";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#handleFallDamage(double,float,net.minecraft.entity.damage.DamageSource)
		static const char* entity_handle_fall_damage_sig = "(DFLnet/minecraft/class_1282;)Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#fallDistance
		static const char* entity_fall_distance_name = "field_6017";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/Entity.html#fallDistance
		static const char* entity_fall_distance_sig = "D";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#attack(net.minecraft.entity.Entity)
		static const char* player_attack_name = "method_7324";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#attack(net.minecraft.entity.Entity)
		static const char* player_attack_sig = "(Lnet/minecraft/class_1297;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#attackLivingEntity(net.minecraft.entity.LivingEntity)
		static const char* player_attack_living_entity_name = "method_5997";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#attackLivingEntity(net.minecraft.entity.LivingEntity)
		static const char* player_attack_living_entity_sig = "(Lnet/minecraft/class_1309;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#inventory
		static const char* player_inventory_name = "field_7514";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerEntity.html#inventory
		static const char* player_inventory_sig = "Lnet/minecraft/class_1661;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html
		static const char* player_inventory_class_sig = "net/minecraft/class_1661";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#selectedSlot
		static const char* inventory_selected_slot_name = "field_7545";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#selectedSlot
		static const char* inventory_selected_slot_sig = "I";
		// 1.21.x: direct SetIntField on selectedSlot no longer propagates to the
		// server (no UpdateSelectedSlotC2SPacket, server keeps thinking we're on
		// the previous slot). Use the new setter instead.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#setSelectedSlot(int)
		static const char* inventory_set_selected_slot_name = "method_61496";
		static const char* inventory_set_selected_slot_sig  = "(I)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#getStack(int)
		static const char* inventory_get_stack_name = "method_5438";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#getStack(int)
		static const char* inventory_get_stack_sig = "(I)Lnet/minecraft/class_1799;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#setStack(int,net.minecraft.item.ItemStack)
		static const char* inventory_set_stack_name = "method_5447";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#setStack(int,net.minecraft.item.ItemStack)
		static const char* inventory_set_stack_sig = "(ILnet/minecraft/class_1799;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#getItem()
		static const char* itemstack_get_item_name = "method_7909";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#getItem()
		static const char* itemstack_get_item_sig = "()Lnet/minecraft/class_1792;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#isEmpty()
		static const char* itemstack_is_empty_name = "method_7960";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/ItemStack.html#isEmpty()
		static const char* itemstack_is_empty_sig = "()Z";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/Item.html
		static const char* item_class_sig = "net/minecraft/class_1792";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/Item.html#getTranslationKey()
		static const char* item_get_translation_key_name = "method_7876";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/item/Item.html#getTranslationKey()
		static const char* item_get_translation_key_sig = "()Ljava/lang/String;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerInteractionManager.html#interactItem(net.minecraft.entity.player.PlayerEntity,net.minecraft.util.Hand)
		static const char* interact_item_name = "method_2919";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayerInteractionManager.html#interactItem(net.minecraft.entity.player.PlayerEntity,net.minecraft.util.Hand)
		static const char* interact_item_sig = "(Lnet/minecraft/class_1657;Lnet/minecraft/class_1268;)Lnet/minecraft/class_1269;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/Hand.html
		static const char* hand_class_sig = "net/minecraft/class_1268";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/Hand.html#MAIN_HAND
		static const char* hand_main_hand_name = "field_5808";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/Hand.html#MAIN_HAND
		static const char* hand_main_hand_sig = "Lnet/minecraft/class_1268;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/EntityHitResult.html
		static const char* entity_hit_result_class_sig = "net/minecraft/class_3966";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/EntityHitResult.html#getEntity()
		static const char* entity_hit_result_get_entity_name = "method_17782";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/EntityHitResult.html#getEntity()
		static const char* entity_hit_result_get_entity_sig = "()Lnet/minecraft/class_1297;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getHealth()
		static const char* living_entity_get_health_name = "method_6032";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getHealth()
		static const char* living_entity_get_health_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getMaxHealth()
		static const char* living_entity_get_max_health_name = "method_6063";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#getMaxHealth()
		static const char* living_entity_get_max_health_sig = "()F";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#hurtTime
		static const char* living_entity_hurt_time_name = "field_6235";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/LivingEntity.html#hurtTime
		static const char* living_entity_hurt_time_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#offHand
		static const char* inventory_offhand_name = "field_30639";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/entity/player/PlayerInventory.html#offHand
		static const char* inventory_offhand_sig = "I";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html
		static const char* client_world_class_sig = "net/minecraft/class_638";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#blockEntities
		static const char* client_world_block_entities_name = "field_60919";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/world/ClientWorld.html#blockEntities
		static const char* client_world_block_entities_sig = "Ljava/util/Set;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/BlockEntity.html
		static const char* block_entity_class_sig = "net/minecraft/class_2586";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/BlockEntity.html#getPos()
		static const char* block_entity_get_pos_name = "method_11016";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/BlockEntity.html#getPos()
		static const char* block_entity_get_pos_sig = "()Lnet/minecraft/class_2338;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html
		static const char* block_pos_class_sig = "net/minecraft/class_2338";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getX()
		static const char* block_pos_get_x_name = "method_10263";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getX()
		static const char* block_pos_get_x_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getY()
		static const char* block_pos_get_y_name = "method_10264";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getY()
		static const char* block_pos_get_y_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getZ()
		static const char* block_pos_get_z_name = "method_10260";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#getZ()
		static const char* block_pos_get_z_sig = "()I";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/world/World.html
		static const char* world_class_sig = "net/minecraft/class_1937";
		// getBlockState(BlockPos): BlockState — inherited from BlockView
		// (method_8320). The previous mapping was wrong (had setBlockState's
		// 3-arg sig, so lookups silently failed).
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/world/BlockView.html#getBlockState(net.minecraft.util.math.BlockPos)
		static const char* world_get_block_state_name = "method_8320";
		static const char* world_get_block_state_sig  = "(Lnet/minecraft/class_2338;)Lnet/minecraft/class_2680;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/AbstractBlock.AbstractBlockState.html
		static const char* block_state_class_sig = "net/minecraft/class_4970$class_4971";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/AbstractBlock.AbstractBlockState.html#getBlock()
		static const char* block_state_get_block_name = "method_26204";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/AbstractBlock.AbstractBlockState.html#getBlock()
		static const char* block_state_get_block_sig = "()Lnet/minecraft/class_2248;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/ChestBlockEntity.html
		static const char* chest_block_entity_class_sig = "net/minecraft/class_2595";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/EnderChestBlockEntity.html
		static const char* ender_chest_block_entity_class_sig = "net/minecraft/class_2611";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/entity/ShulkerBoxBlockEntity.html
		static const char* shulker_box_block_entity_class_sig = "net/minecraft/class_2627";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/BlockHitResult.html
		static const char* block_hit_result_class_sig = "net/minecraft/class_3965";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/BlockHitResult.html#getBlockPos()
		static const char* block_hit_result_get_block_pos_name = "method_17777";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/BlockHitResult.html#getBlockPos()
		static const char* block_hit_result_get_block_pos_sig = "()Lnet/minecraft/class_2338;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/HitResult.html#getType()
		static const char* block_hit_result_get_type_name = "method_17783";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/hit/HitResult.html#getType()
		static const char* block_hit_result_get_type_sig = "()Lnet/minecraft/class_239$class_240;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/ObsidianBlock.html
		static const char* obsidian_block_class_sig = "net/minecraft/class_663$class_11926";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/block/RespawnAnchorBlock.html
		static const char* respawn_anchor_block_class_sig = "net/minecraft/class_4969";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientPlayNetworkHandler.html
		static const char* network_handler_class_sig = "net/minecraft/class_634";
		// 1.21.x: sendPacket lives on ClientCommonNetworkHandler (superclass of
		// ClientPlayNetworkHandler) as method_52787 and takes a Packet (class_2596),
		// not a String. The old (Ljava/lang/String;)V signature was wrong and
		// silently failed every lookup.
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/client/network/ClientCommonNetworkHandler.html#sendPacket(net.minecraft.network.packet.Packet)
		static const char* send_packet_name = "method_52787";
		static const char* send_packet_sig  = "(Lnet/minecraft/class_2596;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/UpdateSelectedSlotC2SPacket.html
		static const char* update_selected_slot_c2s_packet_class_sig = "net/minecraft/class_2868";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket.html
		static const char* player_action_c2s_packet_class_sig = "net/minecraft/class_2846";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html
		static const char* player_action_c2s_packet_action_class_sig = "net/minecraft/class_2846$class_2847";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html#SWAP_ITEM_WITH_OFFHAND
		static const char* swap_item_with_offhand_action_name = "field_12969";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/PlayerActionC2SPacket$Action.html#SWAP_ITEM_WITH_OFFHAND
		static const char* swap_item_with_offhand_action_sig = "Lnet/minecraft/class_2846$class_2847;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/c2s/play/UpdateSelectedSlotC2SPacket.html
		static const char* pick_from_inventory_c2s_packet_class_sig = "net/minecraft/class_2868";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#ORIGIN
		static const char* block_pos_origin_name = "field_10980";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/BlockPos.html#ORIGIN
		static const char* block_pos_origin_sig = "Lnet/minecraft/class_2338;";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Direction.html
		static const char* direction_class_sig = "net/minecraft/class_2350";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Direction.html#DOWN
		static const char* direction_down_name = "field_11033";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/util/math/Direction.html#DOWN
		static const char* direction_down_sig = "Lnet/minecraft/class_2350;";
		static const char* channel_inbound_handler_adapter_class_sig = "io/netty/channel/ChannelInboundHandlerAdapter";
		static const char* channel_read0_name = "channelRead0";
		static const char* channel_read0_sig = "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityS2CPacket.html
		static const char* entity_s2c_packet_class_sig = "net/minecraft/class_2684";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityPositionS2CPacket.html
		static const char* entity_position_s2c_packet_class_sig = "net/minecraft/class_2777";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityMoveS2CPacket.html
		static const char* entity_move_s2c_packet_class_sig = "net/minecraft/class_2777";
		// https://maven.fabricmc.net/docs/yarn-1.21.11+build.5/net/minecraft/network/packet/s2c/play/EntityTeleportS2CPacket.html
		static const char* entity_teleport_s2c_packet_class_sig = "net/minecraft/class_2777";

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
		static const char* keyboard_input_class_sig = "net/minecraft/class_743";
		static const char* input_tick_name = "method_3129";
		static const char* input_tick_sig  = "()V";
		static const char* input_class_sig = "net/minecraft/class_744";
		static const char* input_movement_vector_name = "field_55868";
		static const char* input_movement_vector_sig  = "Lnet/minecraft/class_241;";
		static const char* input_player_input_name = "field_54155";
		static const char* input_player_input_sig  = "Lnet/minecraft/class_10185;";
		static const char* player_input_class_sig  = "net/minecraft/class_10185";

		// Kept as empty strings so movement_correction_hook still compiles and
		// takes its existing "accessor missing -> refuse to attach" path,
		// instead of pretending to work with names that no longer exist.
		static const char* keyboard_input_tick_name = "";
		static const char* keyboard_input_tick_sig  = "(ZF)V";
		static const char* input_forward_name  = "";
		static const char* input_forward_sig   = "F";
		static const char* input_sideways_name = "";
		static const char* input_sideways_sig  = "F";
		// ClientPlayerEntity.input
		static const char* client_player_input_field_name = "field_3913";
		static const char* client_player_input_field_sig  = "Lnet/minecraft/class_744;";

		// MobEntity / AnimalEntity for type filtering.
		static const char* mob_entity_class_sig    = "net/minecraft/class_1308";
		static const char* animal_entity_class_sig = "net/minecraft/class_1429";

		// Entity.isAlive()
		static const char* entity_is_alive_name = "method_5805";
		static const char* entity_is_alive_sig  = "()Z";

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
		static const char* living_entity_get_absorption_name = "method_6067";
		static const char* living_entity_get_absorption_sig  = "()F";

		// ScoreHolder.getNameForScoreboard() -> String, implemented by Entity.
		// The identity servers key teams and friends on, as opposed to the
		// decorated display name that may carry rank prefixes and colours.
		static const char* entity_scoreboard_name_name = "method_5820";
		static const char* entity_scoreboard_name_sig  = "()Ljava/lang/String;";

		// Entity.getScoreboardTeam() -> Team
		static const char* entity_get_team_name = "method_5781";
		static const char* entity_get_team_sig  = "()Lnet/minecraft/class_268;";

		// Entity.getHeight() -> float, for the name-tag anchor.
		static const char* entity_get_height_name = "method_17682";
		static const char* entity_get_height_sig  = "()F";

		// ---- Scoreboard / teams: hiding Minecraft's own floating names ----
		// The technique is the reference's: force every visible player's team to
		// nameTagVisibility = NEVER, parking teamless players in a throwaway
		// team, and put it all back on the way out.
		static const char* world_get_scoreboard_name = "method_8428";
		static const char* world_get_scoreboard_sig  = "()Lnet/minecraft/class_269;";
		static const char* scoreboard_class_sig      = "net/minecraft/class_269";

		// Scoreboard.getScoreHolderTeam(String) -> Team
		static const char* scoreboard_get_holder_team_name = "method_1164";
		static const char* scoreboard_get_holder_team_sig  = "(Ljava/lang/String;)Lnet/minecraft/class_268;";
		// Scoreboard.getTeam(String) -> Team
		static const char* scoreboard_get_team_name = "method_1153";
		static const char* scoreboard_get_team_sig  = "(Ljava/lang/String;)Lnet/minecraft/class_268;";
		// Scoreboard.addTeam(String) -> Team
		static const char* scoreboard_add_team_name = "method_1171";
		static const char* scoreboard_add_team_sig  = "(Ljava/lang/String;)Lnet/minecraft/class_268;";
		// Scoreboard.addScoreHolderToTeam(String, Team) -> boolean
		static const char* scoreboard_add_holder_to_team_name = "method_1172";
		static const char* scoreboard_add_holder_to_team_sig  = "(Ljava/lang/String;Lnet/minecraft/class_268;)Z";
		// Scoreboard.clearTeam(String) -> boolean  (removes a holder from its team)
		static const char* scoreboard_clear_team_name = "method_1195";
		static const char* scoreboard_clear_team_sig  = "(Ljava/lang/String;)Z";

		static const char* team_class_sig          = "net/minecraft/class_268";
		static const char* abstract_team_class_sig = "net/minecraft/class_270";
		// AbstractTeam.getName() -> String
		static const char* team_get_name_name = "method_1197";
		static const char* team_get_name_sig  = "()Ljava/lang/String;";
		// AbstractTeam.getNameTagVisibilityRule() -> VisibilityRule
		static const char* team_get_visibility_name = "method_1201";
		static const char* team_get_visibility_sig  = "()Lnet/minecraft/class_270$class_272;";
		// Team.setNameTagVisibilityRule(VisibilityRule)
		static const char* team_set_visibility_name = "method_1149";
		static const char* team_set_visibility_sig  = "(Lnet/minecraft/class_270$class_272;)V";
		// Team.getPrefix() -> Text
		static const char* team_get_prefix_name = "method_1144";
		static const char* team_get_prefix_sig  = "()Lnet/minecraft/class_2561;";

		static const char* visibility_class_sig  = "net/minecraft/class_270$class_272";
		static const char* visibility_never_name = "field_1443";
		static const char* visibility_sig        = "Lnet/minecraft/class_270$class_272;";

		// LivingEntity.isUsingItem() — for NoAttackWhenEat.
		static const char* living_entity_is_using_item_name = "method_6115";
		static const char* living_entity_is_using_item_sig  = "()Z";

		// Entity.getName() -> Text — used for friends matching.
		static const char* entity_get_name_name = "method_5477";
		static const char* entity_get_name_sig  = "()Lnet/minecraft/class_2561;";
		static const char* text_class_sig = "net/minecraft/class_2561";
		// Not yarn-mangled — Text.getString() comes from the interface.
		static const char* text_get_string_name = "getString";
		static const char* text_get_string_sig  = "()Ljava/lang/String;";

		// LivingEntity body/head yaw — kept in sync with fake yaw inside the
		// silent rotation hook, otherwise other players see the head/body
		// glued to the real yaw and the killaura is visual-detectable.
		static const char* living_entity_body_yaw_name = "field_6283";
		static const char* living_entity_body_yaw_sig  = "F";
		static const char* living_entity_head_yaw_name = "field_6241";
		static const char* living_entity_head_yaw_sig  = "F";

		// BlockItem — used by the autoclicker's RMB "blocks only" gate to
		// confirm the player is currently holding a placeable block (and
		// not eg. food / a tool / a sword).
		static const char* block_item_class_sig = "net/minecraft/class_1747";

		// Input.sneaking — write-target for the Eagle module. Setting this
		// to true on the local player's input is the same path the vanilla
		// keyboard input takes when you hold LShift; MC then drives all the
		// usual side effects (movement clamp at edges, pose, packet) for us.
		// Obsolete with the rest of the Input refactor; Eagle drives sneak
		// through the KeyBinding path below instead, which still resolves.
		static const char* input_sneaking_name = "";
		static const char* input_sneaking_sig  = "Z";

		// BlockState.isAir() — used by Eagle's edge detector.
		static const char* block_state_is_air_name = "method_26215";
		static const char* block_state_is_air_sig  = "()Z";

		// ---- Teams module: armor / equipment access ----
		// LivingEntity.getEquippedStack(EquipmentSlot)
		static const char* living_entity_get_equipped_stack_name = "method_6118";
		static const char* living_entity_get_equipped_stack_sig  = "(Lnet/minecraft/class_1304;)Lnet/minecraft/class_1799;";
		// EquipmentSlot enum
		static const char* equipment_slot_class_sig = "net/minecraft/class_1304";
		static const char* equipment_slot_sig       = "Lnet/minecraft/class_1304;";
		static const char* equipment_slot_head_name  = "field_6169";  // HEAD
		static const char* equipment_slot_chest_name = "field_6174";  // CHEST
		static const char* equipment_slot_legs_name  = "field_6172";  // LEGS
		static const char* equipment_slot_feet_name  = "field_6166";  // FEET
		// Dye colour moved out of LeatherArmorItem entirely: class_1738 and its
		// getColor(ItemStack) are both gone on 1.21.11. The colour now lives in
		// a data component, read through a STATIC helper:
		//   DyedColorComponent.getColor(ItemStack, int fallback) -> int
		// Verified against the official Yarn mappings.
		static const char* dyed_color_component_class_sig = "net/minecraft/class_9282";
		static const char* dyed_color_get_color_name = "method_57470";
		static const char* dyed_color_get_color_sig  = "(Lnet/minecraft/class_1799;I)I";

		// ---- Eagle: KeyBinding override path ----
		// MinecraftClient.options : GameOptions, GameOptions.sneakKey :
		// KeyBinding, KeyBinding.setPressed(boolean). Writing here goes
		// straight into the field that Input.tick() reads as the sneak
		// state — no GLFW round-trip, no fighting KeyboardInput.tick.
		static const char* mc_options_field_name = "field_1690";
		static const char* mc_options_field_sig  = "Lnet/minecraft/class_315;";
		static const char* gameoptions_class_sig = "net/minecraft/class_315";
		static const char* gameoptions_sneak_key_name = "field_1832";
		static const char* gameoptions_sneak_key_sig  = "Lnet/minecraft/class_304;";
		// GameOptions.sprintKey. Same route as sneak, and for the same reason:
		// LivingEntity.setSprinting mutates the entity's attribute-modifier map
		// (the sprint speed boost), and that map is not thread-safe. Calling it
		// from the client's worker thread races the tick thread doing the same
		// inside tickMovement and corrupts the map — observed as
		// ArrayIndexOutOfBoundsException(-1) inside Object2ObjectArrayMap.remove.
		// Pressing the key instead lets Minecraft flip sprinting on its own
		// thread, where the mutation is serialised with everything else.
		static const char* gameoptions_sprint_key_name = "field_1867";
		static const char* gameoptions_sprint_key_sig  = "Lnet/minecraft/class_304;";
		static const char* keybinding_class_sig  = "net/minecraft/class_304";
		static const char* keybinding_set_pressed_name = "method_23481";
		static const char* keybinding_set_pressed_sig  = "(Z)V";

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
		static const char* world_renderer_class_sig = "net/minecraft/class_761";
		static const char* world_renderer_render_name = "method_22710";
		static const char* world_renderer_render_sig  =
			"(Lnet/minecraft/class_9922;Lnet/minecraft/class_9779;ZLnet/minecraft/class_4184;"
			"Lorg/joml/Matrix4f;Lorg/joml/Matrix4f;Lorg/joml/Matrix4f;"
			"Lcom/mojang/blaze3d/buffers/GpuBufferSlice;Lorg/joml/Vector4f;Z)V";

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
		static const char* entity_last_render_x_name = "field_6038";
		static const char* entity_last_render_y_name = "field_5971";
		static const char* entity_last_render_z_name = "field_5989";
		static const char* entity_last_render_sig    = "D";

		// MinecraftClient.renderTickCounter — the field is typed as the concrete
		// RenderTickCounter.Dynamic, while getTickProgress lives on the base.
		static const char* mc_render_tick_counter_name = "field_52750";
		static const char* mc_render_tick_counter_sig  = "Lnet/minecraft/class_9779$class_9781;";
		static const char* render_tick_counter_class_sig = "net/minecraft/class_9779";
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
		static const char* render_tick_counter_progress_name = "method_60637";
		static const char* render_tick_counter_progress_sig  = "(Z)F";

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
		static const char* attack_entity_name = "method_2918";
		static const char* attack_entity_sig  = "(Lnet/minecraft/class_1657;Lnet/minecraft/class_1297;)V";

		// LivingEntity.swingHand(Hand) — plays the arm-swing animation AND sends
		// the HandSwingC2SPacket. Vanilla doAttack() calls this right after
		// attackEntity(); attackEntity alone deals the hit but never swings, so
		// without it there is no animation and no swing packet.
		static const char* swing_hand_name = "method_6104";
		static const char* swing_hand_sig  = "(Lnet/minecraft/class_1268;)V";
		// Hand enum + Hand.MAIN_HAND reuse the existing hand_class_sig /
		// hand_main_hand_name / hand_main_hand_sig defined above.

	}
};

#endif // MAPPINGS_HPP