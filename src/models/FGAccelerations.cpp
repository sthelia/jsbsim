/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

 Module:       FGAccelerations.cpp
 Author:       Jon S. Berndt
 Date started: 07/12/11
 Purpose:      Calculates derivatives of rotational and translational rates, and
               of the attitude quaternion.
 Called by:    FGFDMExec

 ------------- Copyright (C) 2011  Jon S. Berndt (jon@jsbsim.org) -------------

 This program is free software; you can redistribute it and/or modify it under
 the terms of the GNU Lesser General Public License as published by the Free Software
 Foundation; either version 2 of the License, or (at your option) any later
 version.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 details.

 You should have received a copy of the GNU Lesser General Public License along with
 this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 Place - Suite 330, Boston, MA  02111-1307, USA.

 Further information about the GNU Lesser General Public License can also be found on
 the world wide web at http://www.gnu.org.

FUNCTIONAL DESCRIPTION
--------------------------------------------------------------------------------
This class encapsulates the calculation of the derivatives of the state vectors
UVW and PQR - the translational and rotational rates relative to the planet 
fixed frame. The derivatives relative to the inertial frame are also calculated
as a side effect. Also, the derivative of the attitude quaterion is also calculated.

HISTORY
--------------------------------------------------------------------------------
07/12/11   JSB   Created

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
COMMENTS, REFERENCES,  and NOTES
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
[1] Stevens and Lewis, "Aircraft Control and Simulation", Second edition (2004)
    Wiley
[2] Richard E. McFarland, "A Standard Kinematic Model for Flight Simulation at
    NASA-Ames", NASA CR-2497, January 1975
[3] Erin Catto, "Iterative Dynamics with Temporal Coherence", February 22, 2005

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
INCLUDES
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#include "FGAccelerations.h"
#include "FGGroundReactions.h"
#include "FGFDMExec.h"
#include "input_output/FGPropertyManager.h"

using namespace std;

namespace JSBSim {

static const char *IdSrc = "$Id: FGAccelerations.cpp,v 1.3 2011/07/24 19:44:13 jberndt Exp $";
static const char *IdHdr = ID_ACCELERATIONS;

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
CLASS IMPLEMENTATION
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

FGAccelerations::FGAccelerations(FGFDMExec* fdmex)
  : FGModel(fdmex)
{
  Debug(0);
  Name = "FGAccelerations";
  gravType = gtWGS84;
 
  vPQRidot.InitMatrix();
  vUVWidot.InitMatrix();
  vGravAccel.InitMatrix();
  vBodyAccel.InitMatrix();
  vQtrndot = FGQuaternion(0,0,0);

  bind();
  Debug(0);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

FGAccelerations::~FGAccelerations(void)
{
  Debug(1);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

bool FGAccelerations::InitModel(void)
{
  vPQRidot.InitMatrix();
  vUVWidot.InitMatrix();
  vGravAccel.InitMatrix();
  vBodyAccel.InitMatrix();
  vQtrndot = FGQuaternion(0,0,0);

  return true;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
/*
Purpose: Called on a schedule to calculate derivatives.
*/

bool FGAccelerations::Run(bool Holding)
{
  if (FGModel::Run(Holding)) return true;  // Fast return if we have nothing to do ...
  if (Holding) return false;

  RunPreFunctions();

  CalculatePQRdot();   // Angular rate derivative
  CalculateUVWdot();   // Translational rate derivative
  CalculateQuatdot();  // Angular orientation derivative

  ResolveFrictionForces(in.DeltaT * rate);  // Update rate derivatives with friction forces

  RunPostFunctions();

  Debug(2);
  return false;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Compute body frame rotational accelerations based on the current body moments
//
// vPQRdot is the derivative of the absolute angular velocity of the vehicle 
// (body rate with respect to the inertial frame), expressed in the body frame,
// where the derivative is taken in the body frame.
// J is the inertia matrix
// Jinv is the inverse inertia matrix
// vMoments is the moment vector in the body frame
// in.vPQRi is the total inertial angular velocity of the vehicle
// expressed in the body frame.
// Reference: See Stevens and Lewis, "Aircraft Control and Simulation", 
//            Second edition (2004), eqn 1.5-16e (page 50)

void FGAccelerations::CalculatePQRdot(void)
{
  // Compute body frame rotational accelerations based on the current body
  // moments and the total inertial angular velocity expressed in the body
  // frame.

  vPQRidot = in.Jinv * (in.Moment - in.vPQRi * (in.J * in.vPQRi));
  vPQRdot = vPQRidot - in.vPQRi * (in.Ti2b * in.vOmegaPlanet);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Compute the quaternion orientation derivative
//
// vQtrndot is the quaternion derivative.
// Reference: See Stevens and Lewis, "Aircraft Control and Simulation", 
//            Second edition (2004), eqn 1.5-16b (page 50)

void FGAccelerations::CalculateQuatdot(void)
{
  // Compute quaternion orientation derivative on current body rates
  vQtrndot = in.qAttitudeECI.GetQDot(in.vPQRi);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// This set of calculations results in the body and inertial frame accelerations
// being computed.
// Compute body and inertial frames accelerations based on the current body
// forces including centripetal and coriolis accelerations for the former.
// in.vOmegaPlanet is the Earth angular rate - expressed in the inertial frame -
//   so it has to be transformed to the body frame. More completely,
//   in.vOmegaPlanet is the rate of the ECEF frame relative to the Inertial
//   frame (ECI), expressed in the Inertial frame.
// in.Force is the total force on the vehicle in the body frame.
// in.vPQR is the vehicle body rate relative to the ECEF frame, expressed
//   in the body frame.
// in.vUVW is the vehicle velocity relative to the ECEF frame, expressed
//   in the body frame.
// Reference: See Stevens and Lewis, "Aircraft Control and Simulation", 
//            Second edition (2004), eqns 1.5-13 (pg 48) and 1.5-16d (page 50)

void FGAccelerations::CalculateUVWdot(void)
{
  vBodyAccel = in.Force / in.Mass;

  vUVWdot = vBodyAccel - (in.vPQR + 2.0 * (in.Ti2b * in.vOmegaPlanet)) * in.vUVW;

  // Include Centripetal acceleration.
  vUVWdot -= in.Ti2b * (in.vOmegaPlanet * (in.vOmegaPlanet * in.vInertialPosition));

  // Include Gravitation accel
  switch (gravType) {
    case gtStandard:
      vGravAccel = in.Tl2b * FGColumnVector3( 0.0, 0.0, in.GAccel );
      break;
    case gtWGS84:
      vGravAccel = in.Tec2b * in.J2Grav;
      break;
  }

  vUVWdot += vGravAccel;
  vUVWidot = in.Tb2i * (vBodyAccel + vGravAccel);
}

#if 0
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Evaluates the rates (translation or rotation) that the friction forces have
// to resist to. This includes the external forces and moments as well as the
// relative movement between the aircraft and the ground.
// Erin Catto's paper (see ref [3]) only supports Euler integration scheme and
// this algorithm has been adapted to handle the multistep algorithms that
// JSBSim supports (i.e. Trapezoidal, Adams-Bashforth 2, 3 and 4). The capacity
// to handle the multistep integration schemes adds some complexity but it
// significantly helps stabilizing the friction forces.

void FGAccelerations::EvaluateRateToResistTo(FGColumnVector3& vdot,
                                         const FGColumnVector3& Val,
                                         const FGColumnVector3& ValDot,
                                         const FGColumnVector3& LocalTerrainVal,
                                         deque <FGColumnVector3>& dqValDot,
                                         const double dt,
                                         const eIntegrateType integration_type)
{
  switch(integration_type) {
  case eAdamsBashforth4:
    vdot = ValDot + in.Ti2b * (-59.*dqValDot[0]+37.*dqValDot[1]-9.*dqValDot[2])/55.;
    if (dt > 0.) // Zeroes out the relative movement between aircraft and ground
      vdot += 24.*(Val - in.Tec2b * LocalTerrainVal) / (55.*dt);
    break;
  case eAdamsBashforth3:
    vdot = ValDot + in.Ti2b * (-16.*dqValDot[0]+5.*dqValDot[1])/23.;
    if (dt > 0.) // Zeroes out the relative movement between aircraft and ground
      vdot += 12.*(Val - in.Tec2b * LocalTerrainVal) / (23.*dt);
    break;
  case eAdamsBashforth2:
    vdot = ValDot - in.Ti2b * dqValDot[0]/3.;
    if (dt > 0.) // Zeroes out the relative movement between aircraft and ground
      vdot += 2.*(Val - in.Tec2b * LocalTerrainVal) / (3.*dt);
    break;
  case eTrapezoidal:
    vdot = ValDot + in.Ti2b * dqValDot[0];
    if (dt > 0.) // Zeroes out the relative movement between aircraft and ground
      vdot += 2.*(Val - in.Tec2b * LocalTerrainVal) / dt;
    break;
  case eRectEuler:
    vdot = ValDot;
    if (dt > 0.) // Zeroes out the relative movement between aircraft and ground
      vdot += (Val - in.Tec2b * LocalTerrainVal) / dt;
    break;
  case eNone:
    break;
  }
}
#endif

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Resolves the contact forces just before integrating the EOM.
// This routine is using Lagrange multipliers and the projected Gauss-Seidel
// (PGS) method.
// Reference: See Erin Catto, "Iterative Dynamics with Temporal Coherence", 
//            February 22, 2005
// In JSBSim there is only one rigid body (the aircraft) and there can be
// multiple points of contact between the aircraft and the ground. As a
// consequence our matrix J*M^-1*J^T is not sparse and the algorithm described
// in Catto's paper has been adapted accordingly.
// The friction forces are resolved in the body frame relative to the origin
// (Earth center).

void FGAccelerations::ResolveFrictionForces(double dt)
{
  const double invMass = 1.0 / in.Mass;
  const FGMatrix33& Jinv = in.Jinv;
  vector <FGColumnVector3> JacF, JacM;
  vector<double> lambda, lambdaMin, lambdaMax;
  FGColumnVector3 vdot, wdot;
  FGColumnVector3 Fc, Mc;
  int n = 0;

  // Compiles data from the ground reactions to build up the jacobian matrix
  for (MultiplierIterator it=MultiplierIterator(FDMExec->GetGroundReactions()); *it; ++it, n++) {
    JacF.push_back((*it)->ForceJacobian);
    JacM.push_back((*it)->MomentJacobian);
    lambda.push_back((*it)->value);
    lambdaMax.push_back((*it)->Max);
    lambdaMin.push_back((*it)->Min);
  }

  // If no gears are in contact with the ground then return
  if (!n) return;

  vector<double> a(n*n); // Will contain J*M^-1*J^T
  vector<double> rhs(n);

  // Assemble the linear system of equations
  for (int i=0; i < n; i++) {
    for (int j=0; j < i; j++)
      a[i*n+j] = a[j*n+i]; // Takes advantage of the symmetry of J^T*M^-1*J
    for (int j=i; j < n; j++)
      a[i*n+j] = DotProduct(JacF[i],invMass*JacF[j])+DotProduct(JacM[i],Jinv*JacM[j]);
  }

  // Assemble the RHS member
  FGColumnVector3 terrainVel =  FDMExec->GetGroundCallback()->GetTerrainVelocity();
  FGColumnVector3 terrainAngularVel =  FDMExec->GetGroundCallback()->GetTerrainAngularVelocity();

  // Translation
  vdot = vUVWdot;
  if (dt > 0.) // Zeroes out the relative movement between aircraft and ground
    vdot += (in.vUVW - in.Tec2b * terrainVel) / dt;

  // Rotation
  wdot = vPQRdot;
  if (dt > 0.) // Zeroes out the relative movement between aircraft and ground
    wdot += (in.vPQR - in.Tec2b * terrainAngularVel) / dt;

  // Prepare the linear system for the Gauss-Seidel algorithm :
  // 1. Compute the right hand side member 'rhs'
  // 2. Divide every line of 'a' and 'rhs' by a[i,i]. This is in order to save
  //    a division computation at each iteration of Gauss-Seidel.
  for (int i=0; i < n; i++) {
    double d = 1.0 / a[i*n+i];

    rhs[i] = -(DotProduct(JacF[i],vdot)+DotProduct(JacM[i],wdot))*d;
    for (int j=0; j < n; j++)
      a[i*n+j] *= d;
  }

  // Resolve the Lagrange multipliers with the projected Gauss-Seidel method
  for (int iter=0; iter < 50; iter++) {
    double norm = 0.;

    for (int i=0; i < n; i++) {
      double lambda0 = lambda[i];
      double dlambda = rhs[i];
      
      for (int j=0; j < n; j++)
        dlambda -= a[i*n+j]*lambda[j];

      lambda[i] = Constrain(lambdaMin[i], lambda0+dlambda, lambdaMax[i]);
      dlambda = lambda[i] - lambda0;

      norm += fabs(dlambda);
    }

    if (norm < 1E-5) break;
  }

  // Calculate the total friction forces and moments

  Fc.InitMatrix();
  Mc.InitMatrix();

  for (int i=0; i< n; i++) {
    Fc += lambda[i]*JacF[i];
    Mc += lambda[i]*JacM[i];
  }

  FGColumnVector3 accel = invMass * Fc;
  FGColumnVector3 omegadot = Jinv * Mc;

  vUVWdot += accel;
  vUVWidot += in.Tb2i * accel;
  vPQRdot += omegadot;
  vPQRidot += omegadot;

  // Save the value of the Lagrange multipliers to accelerate the convergence
  // of the Gauss-Seidel algorithm at next iteration.
  int i = 0;
  for (MultiplierIterator it=MultiplierIterator(FDMExec->GetGroundReactions()); *it; ++it)
    (*it)->value = lambda[i++];

  FDMExec->GetGroundReactions()->UpdateForcesAndMoments();
}
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void FGAccelerations::InitializeDerivatives(void)
{
  // Make an initial run and set past values
  CalculatePQRdot();           // Angular rate derivative
  CalculateUVWdot();           // Translational rate derivative
  CalculateQuatdot();          // Angular orientation derivative
  ResolveFrictionForces(0.);   // Update rate derivatives with friction forces
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void FGAccelerations::bind(void)
{
  typedef double (FGAccelerations::*PMF)(int) const;

  PropertyManager->Tie("accelerations/pdot-rad_sec2", this, eP, (PMF)&FGAccelerations::GetPQRdot);
  PropertyManager->Tie("accelerations/qdot-rad_sec2", this, eQ, (PMF)&FGAccelerations::GetPQRdot);
  PropertyManager->Tie("accelerations/rdot-rad_sec2", this, eR, (PMF)&FGAccelerations::GetPQRdot);

  PropertyManager->Tie("accelerations/udot-ft_sec2", this, eU, (PMF)&FGAccelerations::GetUVWdot);
  PropertyManager->Tie("accelerations/vdot-ft_sec2", this, eV, (PMF)&FGAccelerations::GetUVWdot);
  PropertyManager->Tie("accelerations/wdot-ft_sec2", this, eW, (PMF)&FGAccelerations::GetUVWdot);

  PropertyManager->Tie("simulation/gravity-model", &gravType);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
//    The bitmasked value choices are as follows:
//    unset: In this case (the default) JSBSim would only print
//       out the normally expected messages, essentially echoing
//       the config files as they are read. If the environment
//       variable is not set, debug_lvl is set to 1 internally
//    0: This requests JSBSim not to output any messages
//       whatsoever.
//    1: This value explicity requests the normal JSBSim
//       startup messages
//    2: This value asks for a message to be printed out when
//       a class is instantiated
//    4: When this value is set, a message is displayed when a
//       FGModel object executes its Run() method
//    8: When this value is set, various runtime state variables
//       are printed out periodically
//    16: When set various parameters are sanity checked and
//       a message is printed out when they go out of bounds

void FGAccelerations::Debug(int from)
{
  if (debug_lvl <= 0) return;

  if (debug_lvl & 1) { // Standard console startup message output
    if (from == 0) { // Constructor

    }
  }
  if (debug_lvl & 2 ) { // Instantiation/Destruction notification
    if (from == 0) cout << "Instantiated: FGAccelerations" << endl;
    if (from == 1) cout << "Destroyed:    FGAccelerations" << endl;
  }
  if (debug_lvl & 4 ) { // Run() method entry print for FGModel-derived objects
  }
  if (debug_lvl & 8 && from == 2) { // Runtime state variables
  }
  if (debug_lvl & 16) { // Sanity checking
  }
  if (debug_lvl & 64) {
    if (from == 0) { // Constructor
      cout << IdSrc << endl;
      cout << IdHdr << endl;
    }
  }
}
}