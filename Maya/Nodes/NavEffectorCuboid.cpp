#include "Precompile.h"
#include "NavEffectorCuboid.h"

#include "Maya/NodeTypes.h"

const MTypeId NavEffectorCuboid::s_TypeID(IGT_NAV_CUBOID_ID);
const char* NavEffectorCuboid::s_TypeName = "igNavEffectorCuboid";

MStatus NavEffectorCuboid::Initialize()
{
  return inheritAttributesFrom( Cuboid::s_TypeName );
}
