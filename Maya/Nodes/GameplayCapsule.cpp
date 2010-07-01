#include "Precompile.h"
#include "GameplayCapsule.h"

#include "Maya/NodeTypes.h"

const MTypeId GameplayCapsule::s_TypeID( IGL_GP_CAPSULE );
const char* GameplayCapsule::s_TypeName = "gpCapsule";

MStatus GameplayCapsule::Initialize()
{ 
  return inheritAttributesFrom( Capsule::s_TypeName );
}
