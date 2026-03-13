/**
 * z64physics.h - Physics strand system structs
 *
 * Original: z64proto/sw97 team (Spaceworld '97 Experience)
 * Adapted for Ship of Harkinian (Shipwright)
 *
 * Verlet integration physics for cloth/strand simulation (Link's hat).
 */
#ifndef Z64PHYSICS_H
#define Z64PHYSICS_H

typedef void (*PhysicCallback)(s32 limbIndex, void*, void*);

typedef struct {
    Vec3f pos;
    Vec3f rot;
    Vec3f vel;
} PhysicsJoint;

typedef struct {
    s32 numLimbs;
    f32 gravity;
    f32 floorY;  // world.pos.y, won't go through this
    f32 maxVel;  // Clamps velocity value
    f32 velStep; // Values below 1.0f will give it spring like motion
    f32 velMult; // Control the power of velocity
} PhysicsInfo;

typedef struct {
    Vec3f pos;
    Vec3s rot;
    MtxF* mtxF;
} PhysicsHead;

typedef struct {
    Gfx* dlist;
    Vec3f scale; // Gfx scale
    u8 segID;    // For matrix
    u8 noDraw;   /* If there needs to be calculations
                    to get in position before drawing */
} PhysicsGfx;

typedef struct {
    s32 num; // amount of spheres
    Vec3f* centers;
    f32 radius;
} PhysicsSpheres; // "collision" spheres, pushes limbs away

typedef struct {
    u8 lockRoot;       // Prevent physics rotating root limb
    Vec2f rotStepCalc; // DEG, limits rot to next limb in main calc
    Vec2f rotStepDraw; // DEG, limits rot on draw, smoothens output
} PhysicsConstraint;

typedef struct {
    s32 num;    // How many limbs will be smoothed with push
    Vec3f push; // direction Z, pushes based on rot[0]
    f32 mult;   // How much the pushing fill affect
    Vec3f rot;  // DEG, rigids towards, relative rot
} PhysicsRigidity;

typedef struct {
    PhysicsInfo info;
    PhysicsHead head;
    PhysicsGfx gfx;
    PhysicsSpheres spheres;
    PhysicsConstraint constraint;
    PhysicsRigidity rigidity;
    f32* limbsLength;
} PhysicsStrand;

// Function declarations
void Physics_GetHeadProperties(PhysicsStrand* strand, Vec3f* mult, s32 flag);
void Physics_SetPhysicsStrand(PhysicsStrand* init, PhysicsStrand* dest, f32* lengthDest, Vec3f* sphereCenters);
Gfx* Physics_DrawDynamicStrand(GraphicsContext* gfxCtx, Gfx* gfx, PhysicsJoint* jointTable, PhysicsStrand* strand,
                               void* callback, void* callbackArg1, void* callbackArg2);

#endif // Z64PHYSICS_H
