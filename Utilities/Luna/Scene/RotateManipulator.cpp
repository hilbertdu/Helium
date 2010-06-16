#include "Precompile.h"
#include "RotateManipulator.h"

#include "Pick.h"
#include "View.h"
#include "Camera.h"
#include "Color.h"

#include "PrimitiveCircle.h"

#include "Scene.h"
#include "ScenePreferences.h"

#include "Editor/Editor.h"

#include "Foundation/Math/AngleAxis.h"

using namespace Math;
using namespace Luna;

LUNA_DEFINE_TYPE(Luna::RotateManipulator);

void RotateManipulator::InitializeType()
{
  Reflect::RegisterClass< Luna::RotateManipulator >( "Luna::RotateManipulator" );
}

void RotateManipulator::CleanupType()
{
  Reflect::UnregisterClass< Luna::RotateManipulator >();
}

RotateManipulator::RotateManipulator(const ManipulatorMode mode, Luna::Scene* scene, Enumerator* enumerator)
: Luna::TransformManipulator (mode, scene, enumerator)
, m_Type (RotationTypes::None)
, m_AxisSnap (false)
, m_SnapDegrees (15.0f)
{
  Luna::ScenePreferences* prefs = SceneEditorPreferences();
  prefs->Get( prefs->RotateManipulatorSize(), m_Size );
  prefs->Get( prefs->RotateManipulatorAxisSnap(), m_AxisSnap );
  prefs->Get( prefs->RotateManipulatorSnapDegrees(), m_SnapDegrees );

  prefs->GetEnum( prefs->RotateManipulatorSpace(), m_Space );

  m_Ring = new Luna::PrimitiveCircle (m_Scene->GetView()->GetResources());
  m_Ring->m_RadiusSteps = 360;
  m_Ring->Update();
}

RotateManipulator::~RotateManipulator()
{
  ScenePreferences* prefs = SceneEditorPreferences();
  prefs->Set( prefs->RotateManipulatorSize(), m_Size );
  prefs->Set( prefs->RotateManipulatorAxisSnap(), m_AxisSnap );
  prefs->Set( prefs->RotateManipulatorSnapDegrees(), m_SnapDegrees );
  prefs->SetEnum( prefs->RotateManipulatorSpace(), m_Space );

  delete m_Ring;
}

void RotateManipulator::ResetSize()
{
  m_Ring->m_Radius = 1.0f;
}

void RotateManipulator::ScaleTo(float factor)
{
  ResetSize();

  m_Ring->m_Radius *= factor;
  m_Ring->Update();
}

void RotateManipulator::Evaluate()
{
  RotateManipulatorAdapter* primary = PrimaryObject<RotateManipulatorAdapter>();

  if (primary)
  {
    // get the transform for our object
    Matrix4 frame = primary->GetFrame(m_Space);

    // compute the scaling factor
    float factor = m_View->GetCamera()->ScalingTo(Vector3 (frame.t.x, frame.t.y, frame.t.z));

    // scale this
    ScaleTo(factor * m_Size);
  }
}

void RotateManipulator::SetResult()
{
  if (m_Manipulated)
  {
    m_Manipulated = false;

    if (!m_ManipulationStart.empty())
    {
      RotateManipulatorAdapter* primary = PrimaryObject<RotateManipulatorAdapter>();

      if (primary != NULL)
      {
        if (!primary->GetNode()->GetScene()->IsEditable())
        {
          for each (RotateManipulatorAdapter* accessor in CompleteSet<RotateManipulatorAdapter>())
          {
            Vector3 val = m_ManipulationStart.find(accessor)->second.m_StartValue;

            accessor->SetValue(EulerAngles (val));
          }
        }
        else
        {
          Undo::BatchCommandPtr batch = new Undo::BatchCommand ();

          for each (RotateManipulatorAdapter* accessor in CompleteSet<RotateManipulatorAdapter>())
          {
            // get current (resultant) value
            Vector3 result = accessor->GetValue().angles;

            // set start value without undo support so its set for handling undo state
            accessor->SetValue(EulerAngles (m_ManipulationStart.find(accessor)->second.m_StartValue));

            // set result with undo support
            batch->Push( accessor->SetValue(EulerAngles (result)) );
          }

          m_Scene->Push( batch );
        }

        // apply modification
        primary->GetNode()->GetScene()->Execute(false);
      }
    }
  }
}

void RotateManipulator::Draw( DrawArgs* args )
{
  RotateManipulatorAdapter* primary = PrimaryObject<RotateManipulatorAdapter>();

  if (primary == NULL)
  {
    return;
  }

  // get the transform for our object
  Matrix4 frame = primary->GetFrame(m_Space).Normalized();
  Vector3 position = Vector3 (frame.t.x, frame.t.y, frame.t.z);

  // rotation from the circle axis to the camera direction
  Vector3 cameraPosition;
  m_View->GetCamera()->GetPosition(cameraPosition);
  Matrix4 toCamera = Matrix4 ( AngleAxis::Rotation(Vector3::BasisX, cameraPosition - position) ) * Matrix4 (position);

  // render x
  Matrix4 x = frame;
  m_View->GetDevice()->SetTransform(D3DTS_WORLD, (D3DMATRIX*)(&x));
  SetAxisMaterial(MultipleAxes::X);
  m_Ring->DrawHiddenBack(args, m_View->GetCamera(), x);

  // render y
  Matrix4 y = Matrix4::RotateZ((float)(Math::HalfPi)) * frame;
  m_View->GetDevice()->SetTransform(D3DTS_WORLD, (D3DMATRIX*)(&y));
  SetAxisMaterial(MultipleAxes::Y);
  m_Ring->DrawHiddenBack(args, m_View->GetCamera(), y);

  // render z
  Matrix4 z = Matrix4::RotateY(-(float)(Math::HalfPi)) * frame;
  m_View->GetDevice()->SetTransform(D3DTS_WORLD, (D3DMATRIX*)(&z));
  SetAxisMaterial(MultipleAxes::Z);
  m_Ring->DrawHiddenBack(args, m_View->GetCamera(), z);

  // render arcball sphere
  m_AxisMaterial.Ambient = Luna::Color::LIGHTGRAY;
  m_View->GetDevice()->SetMaterial(&m_AxisMaterial);
  m_View->GetDevice()->SetTransform(D3DTS_WORLD, (D3DMATRIX*)&toCamera);
  m_Ring->Draw(args);

  // render camera plane ring
  if (m_SelectedAxes == MultipleAxes::All && m_Type == RotationTypes::CameraPlane)
  {
    m_AxisMaterial.Ambient = Luna::Color::YELLOW;
  }
  else
  {
    m_AxisMaterial.Ambient = Luna::Color::SKYBLUE;
  }

  m_View->GetDevice()->SetMaterial(&m_AxisMaterial);
  m_View->GetDevice()->SetTransform(D3DTS_WORLD, (D3DMATRIX*)&(Matrix4 (Scale(1.2f, 1.2f, 1.2f)) * toCamera));
  m_Ring->Draw(args);
}

bool RotateManipulator::Pick( PickVisitor* pick )
{
  RotateManipulatorAdapter* primary = PrimaryObject<RotateManipulatorAdapter>();

  if (primary == NULL || pick->GetPickType() != PickTypes::Line )
  {
    return false;
  }

  // get the transform for our object
  Matrix4 frame = primary->GetFrame(m_Space).Normalized();
  Vector3 position = Vector3 (frame.t.x, frame.t.y, frame.t.z);

  // setup the pick object
  LinePickVisitor* linePick = dynamic_cast<LinePickVisitor*>(pick);
  linePick->SetCurrentObject (this, frame);
  linePick->ClearHits();

  // amount of error allowed to cause a pick hit
  f32 pickRingError = m_Ring->m_Radius / 10.f;

  // pick for a one of the axis ring using the pick transformed into the local space of the object
  m_SelectedAxes = PickRing(pick, pickRingError);

  // if we did not get an axis, check for intersection of our camera plane ring
  if (m_SelectedAxes == MultipleAxes::None)
  {
    float dist, min = m_Ring->m_Radius * 1.2f;
    float stepAngle = (float)(Math::TwoPi) / (float)(m_Ring->m_RadiusSteps);

    // rotation from the circle axis to the camera direction
    Vector3 cameraPosition;
    m_View->GetCamera()->GetPosition(cameraPosition);
    Matrix4 fixup = Matrix4 (AngleAxis::Rotation(Vector3::BasisX, cameraPosition - position)) * Matrix4 (position) * frame.Inverted();//bring the fix up into local space

    // for each point on the ring
    for (int x=0; x<m_Ring->m_RadiusSteps; x++)
    {
      float theta = (float)(x) * stepAngle;

      Vector3 v (0.0f,
                (float)(cos(theta)) * m_Ring->m_Radius * 1.2f,
                (float)(sin(theta)) * m_Ring->m_Radius * 1.2f);

      v += Vector3 (0.0f,
                    (float)(cos(theta + stepAngle)) * m_Ring->m_Radius * 1.2f,
                    (float)(sin(theta + stepAngle)) * m_Ring->m_Radius * 1.2f);

      v *= 0.5f;

      // point it toward the camera
      fixup.TransformVertex(v);

      // perform intersection, storing a hit when we are within our error and it is the closest one so far
      if ( linePick->PickPoint(v, pickRingError) )
      {
        dist = linePick->GetHits().back()->GetIntersectionDistance();

        if (dist >= 0.0f && dist < min)
        {
          min = dist;
        }
      }
    }

    // if we got a hit
    if (min < pickRingError * 1.2f)
    {
      // set camera plane manipulation mode
      m_SelectedAxes = MultipleAxes::All;
      m_Type = RotationTypes::CameraPlane;
    }
  }

  // if we STILL dont have an axis to rotate around
  if (m_SelectedAxes == MultipleAxes::None)
  {
    // check for sphere intersection to perform arcball rotation   
    if( linePick->GetWorldSpaceLine().IntersectsSphere(position, m_Ring->m_Radius) )
    {
      m_SelectedAxes = MultipleAxes::All;
      m_Type = RotationTypes::ArcBall;
    }
  }

  // set fallback type
  if (m_SelectedAxes != MultipleAxes::All)
  {
    m_Type = RotationTypes::Normal;
  }

  if (m_SelectedAxes != MultipleAxes::None)
  {
    return true;
  }
  else
  {
    return false;
  }
}

AxesFlags RotateManipulator::PickRing(PickVisitor* pick, float err)
{
  RotateManipulatorAdapter* primary = PrimaryObject<RotateManipulatorAdapter>();

  if (primary == NULL)
  {
    return MultipleAxes::None;
  }

  float radius = m_Ring->m_Radius;
  float dist, minX = Math::BigFloat, minY = Math::BigFloat, minZ = Math::BigFloat;
  float stepAngle = (float)Math::TwoPi / (float)(m_Ring->m_RadiusSteps);

  Matrix4 frame = primary->GetFrame(m_Space).Normalized();
  Vector3 position = Vector3 (frame.t.x, frame.t.y, frame.t.z);

  Vector3 cameraPosition;
  m_View->GetCamera()->GetPosition(cameraPosition);
  Vector3 cameraVector = cameraPosition - position;

  pick->SetCurrentObject (this, frame);

  for (int x=0; x<m_Ring->m_RadiusSteps; x++)
  {
    float theta = (float)(x) * stepAngle;

    Vector3 v (0.0f,
               (float)(cos(theta)) * radius,
               (float)(sin(theta)) * radius);
    
    v += Vector3 (0.0f,
                  (float)(cos(theta + stepAngle)) * radius,
                  (float)(sin(theta + stepAngle)) * radius);
    
    v *= 0.5f;

    Vector3 transformed = v;
    frame.TransformVertex(transformed);

    if ((transformed - position).Dot(cameraVector) >= 0.0f)
    {
      if (pick->PickPoint(v, err) )       
      {
        dist = pick->GetHits().back()->GetIntersectionDistance();
        if (dist > 0.0f && dist < minX)
        {
          minX = dist;
        }
      }
    }
  }

  for (int x=0; x<m_Ring->m_RadiusSteps; x++)
  {
    float theta = (float)(x) * stepAngle;

    Vector3 v ((float)(cos(theta)) * radius,
               0.0f,
               (float)(sin(theta)) * radius);
    
    v += Vector3 ((float)(cos(theta + stepAngle)) * radius,
                  0.0f,
                  (float)(sin(theta + stepAngle)) * radius);

    v *= 0.5f;

    Vector3 transformed = v;
    frame.TransformVertex(transformed);

    if ((transformed - position).Dot(cameraVector) >= 0.0f)
    {
      if (pick->PickPoint(v, err))
      {
        dist = pick->GetHits().back()->GetIntersectionDistance();
        if (dist > 0.0f && dist < minY)
        {
          minY = dist;
        }
      }
    }
  }

  for (int x=0; x<m_Ring->m_RadiusSteps; x++)
  {
    float theta = (float)(x) * stepAngle;

    Vector3 v ((float)(cos(theta)) * radius,
               (float)(sin(theta)) * radius,
               0.0f);
    
    v += Vector3 ((float)(cos(theta + stepAngle)) * radius,
                  (float)(sin(theta + stepAngle)) * radius,
                  0.0f);
    
    v *= 0.5f;

    Vector3 transformed = v;
    frame.TransformVertex(transformed);

    if ((transformed - position).Dot(cameraVector) >= 0.0f)
    {
      if (pick->PickPoint(v, err))
      {
        dist = pick->GetHits().back()->GetIntersectionDistance();
        if (dist > 0.0f && dist < minZ)
        {      
          minZ = dist;
        }
      }
    }
  }

  if ((minX == minY) && (minY == minZ))
  {
    return MultipleAxes::None;
  }

  if (minX <= minY && minX <= minZ)
  {
    return MultipleAxes::X;
  }

  if (minY <= minX && minY <= minZ)
  {
    return MultipleAxes::Y;
  }

  if (minZ <= minX && minZ <= minY)
  {
    return MultipleAxes::Z;
  }

  return MultipleAxes::None;
}

bool RotateManipulator::MouseDown(wxMouseEvent& e)
{
  Math::AxesFlags previous = m_SelectedAxes;

  LinePickVisitor pick (m_View->GetCamera(), e.GetX(), e.GetY());
  if (!Pick(&pick))
  {
    if (previous != MultipleAxes::All && e.MiddleIsDown())
    {
      m_SelectedAxes = previous;
    }
    else
    {
      return false;
    }
  }

  if (!__super::MouseDown(e))
  {
    return false;
  }

  m_ManipulationStart.clear();

  for each (RotateManipulatorAdapter* accessor in CompleteSet<RotateManipulatorAdapter>())
  {
    ManipulationStart start;
    start.m_StartValue = accessor->GetValue().angles;
    start.m_StartFrame = accessor->GetFrame(m_Space).Normalized();
    start.m_InverseStartFrame = start.m_StartFrame.Inverted();
    m_ManipulationStart.insert( M_ManipulationStart::value_type (accessor, start) );
  }

  return true;
}

void RotateManipulator::MouseUp(wxMouseEvent& e)
{
  __super::MouseUp(e);

  m_Type = RotationTypes::None;
}

void RotateManipulator::MouseMove(wxMouseEvent& e)
{
  __super::MouseMove(e);

  RotateManipulatorAdapter* primary = PrimaryObject<RotateManipulatorAdapter>();

  if (primary == NULL || m_ManipulationStart.empty() || (!m_Left && !m_Middle && !m_Right))
  {
    return;
  }

  const ManipulationStart& primaryStart = m_ManipulationStart.find( primary )->second;

  Vector3 startPoint;
  primaryStart.m_StartFrame.TransformVertex(startPoint);

  Vector3 cameraPosition;
  m_View->GetCamera()->ViewportToWorldVertex(e.GetX(), e.GetY(), cameraPosition);


  //
  // Compute our reference vector in global space, from the object
  //  This is an axis normal (direction) for single axis manipulation, or a plane normal for multi-axis manipulation
  //

  // start out with global manipulator axes
  Vector3 reference = GetAxesNormal(m_SelectedAxes);

  switch (m_SelectedAxes)
  {
    case MultipleAxes::X:
    case MultipleAxes::Y:
    case MultipleAxes::Z:
    {
      // use local axes to manipulate
      primaryStart.m_StartFrame.Transform(reference, 0.f);
      break;
    }
  }

  if (m_SelectedAxes != MultipleAxes::All && reference == Vector3::Zero)
  {
    return;
  }

  // Pick ray from our starting location
  Line startRay;
  m_View->GetCamera()->ViewportToLine(m_StartX, m_StartY, startRay);

  // Pick ray from our current location
  Line endRay;
  m_View->GetCamera()->ViewportToLine(e.GetX(), e.GetY(), endRay);

  // Our from and to vectors for angle axis rotation about a rotation plane
  Vector3 p1, p2;
  Vector3 intersection;

  if (m_SelectedAxes != MultipleAxes::All)
  {
    //
    // Axis-Specific
    //

    p1 = reference;
    p1.Normalize();
    p2 = startPoint - cameraPosition;
    p2.Normalize();

    float dot = p1.Dot(p2);
    bool lowAngle = fabs(dot) < 0.15;

    // if our ray intersects the manipulation sphere
    if (lowAngle && ClosestSphericalIntersection(startRay, startPoint, m_Ring->m_Radius, cameraPosition, intersection))
    {
      // Project onto rotation plane
      Line projection = Line (intersection, intersection + reference);
      projection.IntersectsPlane(Plane (startPoint, reference), &p1);
    }
    // else we are outside the manpiulation sphere
    else if (!lowAngle)
    {
      // Intersection with rotation plane
      if (!startRay.IntersectsPlane(Plane (startPoint, reference), &p1))
      {
        return;
      }
    }
    else
    {
      return;
    }

    // if our ray intersects the manipulation sphere
    if (lowAngle && ClosestSphericalIntersection(endRay, startPoint, m_Ring->m_Radius, cameraPosition, intersection))
    {
      // Project onto rotation plane
      Line projection = Line (intersection, intersection + reference);
      projection.IntersectsPlane(Plane (startPoint, reference), &p2);
    }
    // else we are outside the manpiulation sphere
    else if (!lowAngle)
    {
      // Intersection with rotation plane
      if (!endRay.IntersectsPlane(Plane (startPoint, reference), &p2))
      {
        return;
      }
    }
    else
    {
      return;
    }
  }
  else
  {
    //
    // ArcBall and View Plane
    //

    if (m_Type == RotationTypes::ArcBall)
    {
      if (ClosestSphericalIntersection(startRay, startPoint, m_Ring->m_Radius, cameraPosition, intersection))
      {
        p1 = intersection;
      }
      else
      {
        return;
      }

      if (ClosestSphericalIntersection(endRay, startPoint, m_Ring->m_Radius, cameraPosition, intersection))
      {
        p2 = intersection;
      }
      else
      {
        return;
      }
    }
    else
    {
      // we are rotating in the camera plane, get the reference vector (camera dir)
      m_View->GetCamera()->GetDirection(reference);

      // Intersection with rotation plane
      if (!startRay.IntersectsPlane(Plane (startPoint, reference), &p1))
      {
        return;
      }

      // Intersection with rotation plane
      if (!endRay.IntersectsPlane(Plane (startPoint, reference), &p2))
      {
        return;
      }
    }
  }


  //
  // Now we have our to/from vectors, get the differential rotation around reference by the angle betweeen them
  //

  Vector3 a = p1 - startPoint;
  Vector3 b = p2 - startPoint;

  a.Normalize();
  b.Normalize();

  float angle = (float)(acos(a.Dot(b)));

  // m_ArcBall rotation axis is the axis from vector a to vector b
  if (m_SelectedAxes == MultipleAxes::All && m_Type == RotationTypes::ArcBall)
  {
    reference = a.Cross(b);
    reference.Normalize();
  }

  // always spin the right way, regardless of vector orientation
  if (reference.Dot(a.Cross(b)) < 0.0f)
  {
    angle = -angle;
  }
  
  switch (m_SelectedAxes)
  {
    case MultipleAxes::X:
    case MultipleAxes::Y:
    case MultipleAxes::Z:
      if ( m_AxisSnap )
      {
        float minAngle = m_SnapDegrees * Math::DegToRad;
        float absAngle = fabs( angle );
        int count = absAngle / minAngle;
        if ( angle < 0.0f )
        {
          count = -count;
        }
        angle = count * minAngle;
      }
  }

  // perform rotation
  Matrix4 rotation = Matrix4 (AngleAxis (angle, reference));


  //
  // Convert differential rotation from Global to Local
  //

  Matrix4 parentMatrix;
  Matrix4 inverseParentMatrix;
  
  Luna::Transform* transform = primary->GetNode()->GetTransform();
  NOC_ASSERT(transform);
  if (transform)
  {
    parentMatrix = transform->GetParentTransform();
    inverseParentMatrix = transform->GetInverseParentTransform();
  }

  // reorient rotation in local space
  rotation = parentMatrix * rotation * inverseParentMatrix;


  //
  // Set Value
  //

  for each (RotateManipulatorAdapter* target in CompleteSet<RotateManipulatorAdapter>())
  {
    //
    // Now we have the local differential rotation, account for the rotation orientation
    //

    const ManipulationStart& start = m_ManipulationStart.find( target )->second;

    // get our starting rotation
    Matrix4 totalRotation ( EulerAngles (start.m_StartValue) );

    // append the current to the starting rotation
    totalRotation *= rotation;

    if (totalRotation.Valid())
    {
      // set our result
      target->SetValue(EulerAngles (totalRotation));
    }
    else
    {
      std::ostringstream str;
      str << totalRotation;
      Log::Warning("Invalid floating point result during rotation: %s\n", str.str().c_str());
    }
  }

  // apply modification
  primary->GetNode()->GetScene()->Execute(true);

  // flag as changed
  m_Manipulated = true;
}

bool RotateManipulator::ClosestSphericalIntersection(Line line, Vector3 spherePosition, float sphereRadius, Vector3 cameraPosition, Vector3& intersection)
{
  V_Vector3 intersections;

  // if our ray intersects the sphere
  if (line.IntersectsSphere(spherePosition, sphereRadius, &intersections))
  {
    // Get point on sphere
    Vector3 closest = intersections[0];

    if (intersections.size() > 1)
    {
      if ((intersections[0] - cameraPosition).Length() < (intersections[1] - cameraPosition).Length())
      {
        closest = intersections[0];
      }
      else
      {
        closest = intersections[1];
      }
    }

    intersection = closest;

    return true;
  }

  intersection = Vector3::Zero;

  return false;
}

void RotateManipulator::CreateProperties()
{
  __super::CreateProperties();

  m_Enumerator->PushPanel("Rotate", true);
  {
    m_Enumerator->PushContainer();
    {
      m_Enumerator->AddLabel("Space");
      Inspect::Choice* choice = m_Enumerator->AddChoice<int>( new Nocturnal::MemberProperty<Luna::RotateManipulator, int> (this, &RotateManipulator::GetSpace, &RotateManipulator::SetSpace) );
      choice->SetDropDown( true );
      Inspect::V_Item items;

      {
        std::ostringstream str;
        str << ManipulatorSpaces::Object;
        items.push_back( Inspect::Item( "Object", str.str() ) );
      }

      {
        std::ostringstream str;
        str << ManipulatorSpaces::Local;
        items.push_back( Inspect::Item( "Local", str.str() ) );
      }

      {
        std::ostringstream str;
        str << ManipulatorSpaces::World;
        items.push_back( Inspect::Item( "World", str.str() ) );
      }

      choice->SetItems( items );
    }
    m_Enumerator->Pop();

    m_Enumerator->PushContainer();
    {
      m_Enumerator->AddLabel("Axis Snap");
      m_Enumerator->AddCheckBox<bool>( new Nocturnal::MemberProperty<Luna::RotateManipulator, bool> (this, &RotateManipulator::GetAxisSnap, &RotateManipulator::SetAxisSnap) );
    }
    m_Enumerator->Pop();

    m_Enumerator->PushContainer();
    {
      m_Enumerator->AddLabel("Snap Degrees");
      m_Enumerator->AddValue<float>( new Nocturnal::MemberProperty<Luna::RotateManipulator, f32> (this, &RotateManipulator::GetSnapDegrees, &RotateManipulator::SetSnapDegrees) );
    }
    m_Enumerator->Pop();
  }
  m_Enumerator->Pop();
}

int RotateManipulator::GetSpace() const
{
  return m_Space;
}

void RotateManipulator::SetSpace(int space)
{
  m_Space = (ManipulatorSpace)space;

  RotateManipulatorAdapter* primary = PrimaryObject<RotateManipulatorAdapter>();

  if (primary != NULL)
  {
    primary->GetNode()->GetScene()->Execute(false);
  }
}

bool RotateManipulator::GetAxisSnap() const
{
  return m_AxisSnap;
}

void RotateManipulator::SetAxisSnap(bool axisSnap)
{
  m_AxisSnap = axisSnap;
}

f32 RotateManipulator::GetSnapDegrees() const
{
  return m_SnapDegrees;
}

void RotateManipulator::SetSnapDegrees(float snapDegrees)
{
  m_SnapDegrees = snapDegrees;
}
