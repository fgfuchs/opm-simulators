/*
  Copyright 2014 IRIS AS
  Copyright 2015 Dr. Blatt - HPC-Simulation-Software & Services
  Copyright 2015 Statoil AS

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef OPM_TIMESTEPCONTROL_HEADER_INCLUDED
#define OPM_TIMESTEPCONTROL_HEADER_INCLUDED

#include <vector>

#include <boost/range/iterator_range.hpp>
#include <opm/simulators/timestepping/TimeStepControlInterface.hpp>

namespace Opm
{
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  A simple iteration count based adaptive time step control.
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class SimpleIterationCountTimeStepControl : public TimeStepControlInterface
    {
    public:
        /// \brief constructor
        /// \param target_iterations  number of desired iterations (e.g. Newton iterations) per time step in one time step
        //  \param decayrate          decayrate of time step when target iterations are not met (should be <= 1)
        //  \param growthrate         growthrate of time step when target iterations are not met (should be >= 1)
        /// \param verbose            if true get some output (default = false)
        SimpleIterationCountTimeStepControl( const int target_iterations,
                                             const double decayrate,
                                             const double growthrate,
                                             const bool verbose = false);

        /// \brief \copydoc TimeStepControlInterface::computeTimeStepSize
        double computeTimeStepSize( const double dt, const int iterations, const RelativeChangeInterface& /* relativeChange */, const double /*simulationTimeElapsed */ ) const;

    protected:
        const int     target_iterations_;
        const double  decayrate_;
        const double  growthrate_;
        const bool    verbose_;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  PID controller based adaptive time step control as suggested in:
    ///     Turek and Kuzmin. Algebraic Flux Correction III. Incompressible Flow Problems. Uni Dortmund.
    ///
    ///  See also:
    ///     D. Kuzmin and S.Turek. Numerical simulation of turbulent bubbly flows. Techreport Uni Dortmund. 2004
    ///
    ///  and the original article:
    ///     Valli, Coutinho, and Carey. Adaptive Control for Time Step Selection in Finite Element
    ///     Simulation of Coupled Viscous Flow and Heat Transfer. Proc of the 10th
    ///     International Conference on Numerical Methods in Fluids. 1998.
    ///
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class PIDTimeStepControl : public TimeStepControlInterface
    {
    public:
        /// \brief constructor
        /// \param tol      tolerance for the relative changes of the numerical solution to be accepted
        ///                 in one time step (default is 1e-3)
        /// \param verbose  if true get some output (default = false)
        PIDTimeStepControl( const double tol = 1e-3,
                            const bool verbose = false );

        /// \brief \copydoc TimeStepControlInterface::computeTimeStepSize
        double computeTimeStepSize( const double dt, const int /* iterations */, const RelativeChangeInterface& relativeChange, const double /*simulationTimeElapsed */ ) const;

    protected:
        const double tol_;
        mutable std::vector< double > errors_;

        const bool verbose_;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  PID controller based adaptive time step control as above that also takes
    ///  an target iteration into account.
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class PIDAndIterationCountTimeStepControl : public PIDTimeStepControl
    {
        typedef PIDTimeStepControl BaseType;
    public:
        /// \brief constructor
        /// \param target_iterations  number of desired iterations per time step
        /// \param tol        tolerance for the relative changes of the numerical solution to be accepted
        ///                   in one time step (default is 1e-3)
        /// \param verbose    if true get some output (default = false)
        PIDAndIterationCountTimeStepControl( const int target_iterations = 20,
                                             const double decayDampingFactor = 1.0,
                                             const double growthDampingFactor = 1.0/1.2,
                                             const double tol = 1e-3,
                                             const double minTimeStepBasedOnIterations = 0.,
                                             const bool verbose = false);

        /// \brief \copydoc TimeStepControlInterface::computeTimeStepSize
        double computeTimeStepSize( const double dt, const int iterations, const RelativeChangeInterface& relativeChange, const double /*simulationTimeElapsed */ ) const;

    protected:
        const int     target_iterations_;
        const double  decayDampingFactor_;
        const double  growthDampingFactor_;
        const double  minTimeStepBasedOnIterations_;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///
    ///  HardcodedTimeStepControl
    ///  Input generated from summary file using the ert application:
    ///
    ///  ecl_summary DECK TIME > filename
    ///
    ///  Assumes time is given in days
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    class HardcodedTimeStepControl : public TimeStepControlInterface
    {
    public:
        /// \brief constructor
        /// \param filename   filename contaning the timesteps
        explicit HardcodedTimeStepControl( const std::string& filename);

        /// \brief \copydoc TimeStepControlInterface::computeTimeStepSize
        double computeTimeStepSize( const double dt, const int /* iterations */, const RelativeChangeInterface& /*relativeChange */, const double simulationTimeElapsed) const;

    protected:
        // store the time (in days) of the substeps the simulator should use
        std::vector<double> subStepTime_;
    };


} // end namespace Opm
#endif

