/*
  Copyright 2016 SINTEF ICT, Applied Mathematics.
  Copyright 2016 Statoil ASA.
  Copyright 2016 IRIS AS

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


#ifndef OPM_STANDARDWELLSDENSE_HEADER_INCLUDED
#define OPM_STANDARDWELLSDENSE_HEADER_INCLUDED

#include <opm/common/OpmLog/OpmLog.hpp>

#include <opm/common/utility/platform_dependent/disable_warnings.h>
#include <opm/common/utility/platform_dependent/reenable_warnings.h>

#include <cassert>
#include <tuple>

#include <opm/parser/eclipse/EclipseState/Schedule/Schedule.hpp>

#include <opm/core/wells.h>
#include <opm/core/wells/DynamicListEconLimited.hpp>
#include <opm/core/wells/WellCollection.hpp>
#include <opm/autodiff/VFPProperties.hpp>
#include <opm/autodiff/VFPInjProperties.hpp>
#include <opm/autodiff/VFPProdProperties.hpp>
#include <opm/autodiff/WellHelpers.hpp>
#include <opm/autodiff/BlackoilModelEnums.hpp>
#include <opm/autodiff/WellDensitySegmented.hpp>
#include <opm/autodiff/BlackoilPropsAdFromDeck.hpp>
#include <opm/autodiff/BlackoilDetails.hpp>
#include <opm/autodiff/BlackoilModelParameters.hpp>
#include <opm/autodiff/WellStateFullyImplicitBlackoilDense.hpp>
#include <opm/autodiff/RateConverter.hpp>
#include<dune/common/fmatrix.hh>
#include<dune/istl/bcrsmatrix.hh>
#include<dune/istl/matrixmatrix.hh>

#include <opm/material/densead/Math.hpp>
#include <opm/material/densead/Evaluation.hpp>

#include <opm/simulators/WellSwitchingLogger.hpp>

namespace Opm {

enum WellVariablePositions {
    XvarWell = 0,
    WFrac = 1,
    GFrac = 2
};


        /// Class for handling the standard well model.
        template<typename FluidSystem, typename BlackoilIndices>
        class StandardWellsDense {
        public:

            // ---------      Types      ---------
            typedef WellStateFullyImplicitBlackoilDense WellState;
            typedef BlackoilModelParameters ModelParameters;

            typedef double Scalar;
            static const int blocksize = 3;
            typedef Dune::FieldVector<Scalar, blocksize    > VectorBlockType;
            typedef Dune::FieldMatrix<Scalar, blocksize, blocksize > MatrixBlockType;
            typedef Dune::BCRSMatrix <MatrixBlockType> Mat;
            typedef Dune::BlockVector<VectorBlockType> BVector;
            typedef DenseAd::Evaluation<double, /*size=*/blocksize*2> EvalWell;

            // For the conversion between the surface volume rate and resrevoir voidage rate
            using RateConverterType = RateConverter::
                SurfaceToReservoirVoidage<BlackoilPropsAdFromDeck::FluidSystem, std::vector<int> >;

            // ---------  Public methods  ---------
            StandardWellsDense(const Wells* wells_arg,
                               WellCollection* well_collection,
                               const ModelParameters& param,
                               const bool terminal_output);

            void init(const PhaseUsage phase_usage_arg,
                      const std::vector<bool>& active_arg,
                      const VFPProperties*  vfp_properties_arg,
                      const double gravity_arg,
                      const std::vector<double>& depth_arg,
                      const std::vector<double>& pv_arg,
                      const RateConverterType* rate_converter);


            template <typename Simulator>
            SimulatorReport assemble(Simulator& ebosSimulator,
                                     const int iterationIdx,
                                     const double dt,
                                     WellState& well_state);

            template <typename Simulator>
            void assembleWellEq(Simulator& ebosSimulator,
                                const double dt,
                                WellState& well_state,
                                bool only_wells);

            template <typename Simulator>
            bool allow_cross_flow(const int w, Simulator& ebosSimulator) const;

            void localInvert(Mat& istlA) const;

            void print(Mat& istlA) const;

            // substract Binv(D)rw from r;
            void apply( BVector& r) const;

            // subtract B*inv(D)*C * x from A*x
            void apply(const BVector& x, BVector& Ax);

            // apply well model with scaling of alpha
            void applyScaleAdd(const Scalar alpha, const BVector& x, BVector& Ax);

            // xw = inv(D)*(rw - C*x)
            void recoverVariable(const BVector& x, BVector& xw) const;

            int flowPhaseToEbosCompIdx( const int phaseIdx ) const;

            int flowToEbosPvIdx( const int flowPv ) const;

            int ebosCompToFlowPhaseIdx( const int compIdx ) const;

            std::vector<double>
            extractPerfData(const std::vector<double>& in) const;

            int numPhases() const;

            int numCells() const;

            void resetWellControlFromState(WellState xw);

            const Wells& wells() const;

            const Wells* wellsPointer() const;

            /// return true if wells are available in the reservoir
            bool wellsActive() const;

            void setWellsActive(const bool wells_active);

            /// return true if wells are available on this process
            bool localWellsActive() const;

            int numWellVars() const;

            /// Density of each well perforation
            const std::vector<double>& wellPerforationDensities() const;

            /// Diff to bhp for each well perforation.
            const std::vector<double>& wellPerforationPressureDiffs() const;

            typedef DenseAd::Evaluation<double, /*size=*/blocksize> Eval;

            EvalWell extendEval(Eval in) const;

            void setWellVariables(const WellState& xw);

            void print(EvalWell in) const;

            void computeAccumWells();

            template<typename intensiveQuants>
            void
            computeWellFlux(const int& w, const double& Tw, const intensiveQuants& intQuants, const EvalWell& bhp, const double& cdp, const bool& allow_cf, std::vector<EvalWell>& cq_s)  const
            {
                const Opm::PhaseUsage& pu = phase_usage_;
                const int np = wells().number_of_phases;
                std::vector<EvalWell> cmix_s(np,0.0);
                for (int phase = 0; phase < np; ++phase) {
                    //int ebosPhaseIdx = flowPhaseToEbosPhaseIdx(phase);
                    cmix_s[phase] = wellVolumeFraction(w,phase);
                }
                const auto& fs = intQuants.fluidState();
                EvalWell pressure = extendEval(fs.pressure(FluidSystem::oilPhaseIdx));
                EvalWell rs = extendEval(fs.Rs());
                EvalWell rv = extendEval(fs.Rv());
                std::vector<EvalWell> b_perfcells_dense(np, 0.0);
                std::vector<EvalWell> mob_perfcells_dense(np, 0.0);
                for (int phase = 0; phase < np; ++phase) {
                    int ebosPhaseIdx = flowPhaseToEbosPhaseIdx(phase);
                    b_perfcells_dense[phase] = extendEval(fs.invB(ebosPhaseIdx));
                    mob_perfcells_dense[phase] = extendEval(intQuants.mobility(ebosPhaseIdx));
                }

                // Pressure drawdown (also used to determine direction of flow)
                EvalWell well_pressure = bhp + cdp;
                EvalWell drawdown = pressure - well_pressure;

                // injection perforations
                if ( drawdown.value() > 0 )  {

                    //Do nothing if crossflow is not allowed
                    if (!allow_cf && wells().type[w] == INJECTOR)
                        return;
                    // compute phase volumetric rates at standard conditions
                    std::vector<EvalWell> cq_ps(np, 0.0);
                    for (int phase = 0; phase < np; ++phase) {
                        const EvalWell cq_p = - Tw * (mob_perfcells_dense[phase] * drawdown);
                        cq_ps[phase] = b_perfcells_dense[phase] * cq_p;
                    }

                    if (active_[Oil] && active_[Gas]) {
                        const int oilpos = pu.phase_pos[Oil];
                        const int gaspos = pu.phase_pos[Gas];
                        const EvalWell cq_psOil = cq_ps[oilpos];
                        const EvalWell cq_psGas = cq_ps[gaspos];
                        cq_ps[gaspos] += rs * cq_psOil;
                        cq_ps[oilpos] += rv * cq_psGas;
                    }

                    // map to ADB
                    for (int phase = 0; phase < np; ++phase) {
                        cq_s[phase] = cq_ps[phase];
                    }

                } else {
                    //Do nothing if crossflow is not allowed
                    if (!allow_cf && wells().type[w] == PRODUCER)
                        return;

                    // Using total mobilities
                    EvalWell total_mob_dense = mob_perfcells_dense[0];
                    for (int phase = 1; phase < np; ++phase) {
                        total_mob_dense += mob_perfcells_dense[phase];
                    }
                    // injection perforations total volume rates
                    const EvalWell cqt_i = - Tw * (total_mob_dense * drawdown);

                    // compute volume ratio between connection at standard conditions
                    EvalWell volumeRatio = 0.0;
                    if (active_[Water]) {
                        const int watpos = pu.phase_pos[Water];
                        volumeRatio += cmix_s[watpos] / b_perfcells_dense[watpos];
                    }

                    if (active_[Oil] && active_[Gas]) {
                        EvalWell well_temperature = extendEval(fs.temperature(FluidSystem::oilPhaseIdx));
                        EvalWell rsSatEval = FluidSystem::oilPvt().saturatedGasDissolutionFactor(fs.pvtRegionIndex(), well_temperature, well_pressure);
                        EvalWell rvSatEval = FluidSystem::gasPvt().saturatedOilVaporizationFactor(fs.pvtRegionIndex(), well_temperature, well_pressure);

                        const int oilpos = pu.phase_pos[Oil];
                        const int gaspos = pu.phase_pos[Gas];
                        EvalWell rvPerf = 0.0;
                        if (cmix_s[gaspos] > 0)
                            rvPerf = cmix_s[oilpos] / cmix_s[gaspos];

                        if (rvPerf.value() > rvSatEval.value()) {
                            rvPerf = rvSatEval;
                            //rvPerf.setValue(rvSatEval.value());
                        }

                        EvalWell rsPerf = 0.0;
                        if (cmix_s[oilpos] > 0)
                            rsPerf = cmix_s[gaspos] / cmix_s[oilpos];

                        if (rsPerf.value() > rsSatEval.value()) {
                            //rsPerf = 0.0;
                            rsPerf= rsSatEval;
                        }

                        // Incorporate RS/RV factors if both oil and gas active
                        const EvalWell d = 1.0 - rvPerf * rsPerf;

                        const EvalWell tmp_oil = (cmix_s[oilpos] - rvPerf * cmix_s[gaspos]) / d;
                        //std::cout << "tmp_oil " <<tmp_oil << std::endl;
                        volumeRatio += tmp_oil / b_perfcells_dense[oilpos];

                        const EvalWell tmp_gas = (cmix_s[gaspos] - rsPerf * cmix_s[oilpos]) / d;
                        //std::cout << "tmp_gas " <<tmp_gas << std::endl;
                        volumeRatio += tmp_gas / b_perfcells_dense[gaspos];
                    }
                    else {
                        if (active_[Oil]) {
                            const int oilpos = pu.phase_pos[Oil];
                            volumeRatio += cmix_s[oilpos] / b_perfcells_dense[oilpos];
                        }
                        if (active_[Gas]) {
                            const int gaspos = pu.phase_pos[Gas];
                            volumeRatio += cmix_s[gaspos] / b_perfcells_dense[gaspos];
                        }
                    }
                    // injecting connections total volumerates at standard conditions
                    EvalWell cqt_is = cqt_i/volumeRatio;
                    //std::cout << "volrat " << volumeRatio << " " << volrat_perf_[perf] << std::endl;
                    for (int phase = 0; phase < np; ++phase) {
                        cq_s[phase] = cmix_s[phase] * cqt_is; // * b_perfcells_dense[phase];
                    }
                }
            }

            template <typename Simulator>
            SimulatorReport solveWellEq(Simulator& ebosSimulator,
                                        const double dt,
                                        WellState& well_state)
            {
                const int nw = wells().number_of_wells;
                WellState well_state0 = well_state;

                int it  = 0;
                bool converged;
                do {
                    assembleWellEq(ebosSimulator, dt, well_state, true);
                    converged = getWellConvergence(ebosSimulator, it);

                    // checking whether the group targets are converged
                    if (wellCollection()->groupControlActive()) {
                        converged = converged && wellCollection()->groupTargetConverged(well_state.wellRates());
                    }

                    if (converged) {
                        break;
                    }

                    ++it;
                    if( localWellsActive() )
                    {
                        BVector dx_well (nw);
                        invDuneD_.mv(resWell_, dx_well);

                        updateWellState(dx_well, well_state);
                        updateWellControls(well_state);
                        setWellVariables(well_state);
                    }
                } while (it < 15);

                if (!converged) {
                    well_state = well_state0;
                }

                SimulatorReport report;
                report.converged = converged;
                report.total_well_iterations = it;
                return report;
            }

            void printIf(int c, double x, double y, double eps, std::string type) {
                if (std::abs(x-y) > eps) {
                    std::cout << type << " " << c << ": "<<x << " " << y << std::endl;
                }
            }


            std::vector<double> residual() {
                if( ! wellsActive() )
                {
                    return std::vector<double>();
                }

                const int np = numPhases();
                const int nw = wells().number_of_wells;
                std::vector<double> res(np*nw);
                for( int p=0; p<np; ++p) {
                    const int ebosCompIdx = flowPhaseToEbosCompIdx(p);
                    for (int i = 0; i < nw; ++i) {
                        int idx = i + nw*p;
                        res[idx] = resWell_[ i ][ ebosCompIdx ];
                    }
                }
                return res;
            }


            template <typename Simulator>
            bool getWellConvergence(Simulator& ebosSimulator,
                                    const int iteration)
            {
                typedef std::vector< double > Vector;
                const int np = numPhases();
                const int nc = numCells();
                const double tol_wells = param_.tolerance_wells_;
                const double maxResidualAllowed = param_.max_residual_allowed_;

                Vector R_sum(np);
                Vector B_avg(np);
                Vector maxCoeff(np);
                Vector maxNormWell(np);

                std::vector< Vector > B( np, Vector( nc ) );
                std::vector< Vector > R2( np, Vector( nc ) );
                std::vector< Vector > tempV( np, Vector( nc ) );

                for ( int idx = 0; idx < np; ++idx )
                {
                    Vector& B_idx  = B[ idx ];
                    const int ebosPhaseIdx = flowPhaseToEbosPhaseIdx(idx);

                    for (int cell_idx = 0; cell_idx < nc; ++cell_idx) {
                        const auto& intQuants = *(ebosSimulator.model().cachedIntensiveQuantities(cell_idx, /*timeIdx=*/0));
                        const auto& fs = intQuants.fluidState();

                        B_idx [cell_idx] = 1 / fs.invB(ebosPhaseIdx).value();
                    }
                }

                detail::convergenceReduction(B, tempV, R2,
                                             R_sum, maxCoeff, B_avg, maxNormWell,
                                             nc, np, pv_, residual() );


                Vector well_flux_residual(np);

                bool converged_Well = true;
                // Finish computation
                for ( int idx = 0; idx < np; ++idx )
                {
                    well_flux_residual[idx] = B_avg[idx] * maxNormWell[idx];
                    converged_Well = converged_Well && (well_flux_residual[idx] < tol_wells);
                }

                // if one of the residuals is NaN, throw exception, so that the solver can be restarted
                for (int phaseIdx = 0; phaseIdx < np; ++phaseIdx) {
                    const auto& phaseName = FluidSystem::phaseName(flowPhaseToEbosPhaseIdx(phaseIdx));

                    if (std::isnan(well_flux_residual[phaseIdx])) {
                        OPM_THROW(Opm::NumericalProblem, "NaN residual for phase " << phaseName);
                    }
                    if (well_flux_residual[phaseIdx] > maxResidualAllowed) {
                        OPM_THROW(Opm::NumericalProblem, "Too large residual for phase " << phaseName);
                    }
                }

                if ( terminal_output_ )
                {
                    // Only rank 0 does print to std::cout
                    if (iteration == 0) {
                        std::string msg;
                        msg = "Iter";
                        for (int phaseIdx = 0; phaseIdx < np; ++phaseIdx) {
                            const std::string& phaseName = FluidSystem::phaseName(flowPhaseToEbosPhaseIdx(phaseIdx));
                            msg += "  W-FLUX(" + phaseName + ")";
                        }
                        OpmLog::note(msg);
                    }
                    std::ostringstream ss;
                    const std::streamsize oprec = ss.precision(3);
                    const std::ios::fmtflags oflags = ss.setf(std::ios::scientific);
                    ss << std::setw(4) << iteration;
                    for (int phaseIdx = 0; phaseIdx < np; ++phaseIdx) {
                        ss << std::setw(11) << well_flux_residual[phaseIdx];
                    }
                    ss.precision(oprec);
                    ss.flags(oflags);
                    OpmLog::note(ss.str());
                }
                return converged_Well;
            }



            template<typename Simulator>
            void
            computeWellConnectionPressures(const Simulator& ebosSimulator,
                                           const WellState& xw)
            {
                if( ! localWellsActive() ) return ;
                // 1. Compute properties required by computeConnectionPressureDelta().
                //    Note that some of the complexity of this part is due to the function
                //    taking std::vector<double> arguments, and not Eigen objects.
                std::vector<double> b_perf;
                std::vector<double> rsmax_perf;
                std::vector<double> rvmax_perf;
                std::vector<double> surf_dens_perf;
                computePropertiesForWellConnectionPressures(ebosSimulator, xw, b_perf, rsmax_perf, rvmax_perf, surf_dens_perf);
                computeWellConnectionDensitesPressures(xw, b_perf, rsmax_perf, rvmax_perf, surf_dens_perf, cell_depths_, gravity_);

            }

            template<typename Simulator, class WellState>
            void
            computePropertiesForWellConnectionPressures(const Simulator& ebosSimulator,
                                                        const WellState& xw,
                                                        std::vector<double>& b_perf,
                                                        std::vector<double>& rsmax_perf,
                                                        std::vector<double>& rvmax_perf,
                                                        std::vector<double>& surf_dens_perf)
            {
                const int nperf = wells().well_connpos[wells().number_of_wells];
                const int nw = wells().number_of_wells;
                const PhaseUsage& pu = phase_usage_;
                const int np = phase_usage_.num_phases;
                b_perf.resize(nperf*np);
                surf_dens_perf.resize(nperf*np);

                //rs and rv are only used if both oil and gas is present
                if (pu.phase_used[BlackoilPhases::Vapour] && pu.phase_pos[BlackoilPhases::Liquid]) {
                    rsmax_perf.resize(nperf);
                    rvmax_perf.resize(nperf);
                }

                // Compute the average pressure in each well block
                for (int w = 0; w < nw; ++w) {
                    for (int perf = wells().well_connpos[w]; perf < wells().well_connpos[w+1]; ++perf) {

                        const int cell_idx = wells().well_cells[perf];
                        const auto& intQuants = *(ebosSimulator.model().cachedIntensiveQuantities(cell_idx, /*timeIdx=*/0));
                        const auto& fs = intQuants.fluidState();

                        const double p_above = perf == wells().well_connpos[w] ? xw.bhp()[w] : xw.perfPress()[perf - 1];
                        const double p_avg = (xw.perfPress()[perf] + p_above)/2;
                        double temperature = fs.temperature(FluidSystem::oilPhaseIdx).value();

                        if (pu.phase_used[BlackoilPhases::Aqua]) {
                            b_perf[ pu.phase_pos[BlackoilPhases::Aqua] + perf * pu.num_phases] =
                                    FluidSystem::waterPvt().inverseFormationVolumeFactor(fs.pvtRegionIndex(), temperature, p_avg);
                        }

                        if (pu.phase_used[BlackoilPhases::Vapour]) {
                            int gaspos = pu.phase_pos[BlackoilPhases::Vapour] + perf * pu.num_phases;
                            int gaspos_well = pu.phase_pos[BlackoilPhases::Vapour] + w * pu.num_phases;

                            if (pu.phase_used[BlackoilPhases::Liquid]) {
                                int oilpos_well = pu.phase_pos[BlackoilPhases::Liquid] + w * pu.num_phases;
                                const double oilrate = std::abs(xw.wellRates()[oilpos_well]); //in order to handle negative rates in producers
                                rvmax_perf[perf] = FluidSystem::gasPvt().saturatedOilVaporizationFactor(fs.pvtRegionIndex(), temperature, p_avg);
                                if (oilrate > 0) {
                                    const double gasrate = std::abs(xw.wellRates()[gaspos_well]);
                                    double rv = 0.0;
                                    if (gasrate > 0) {
                                        rv = oilrate / gasrate;
                                    }
                                    rv = std::min(rv, rvmax_perf[perf]);

                                    b_perf[gaspos] = FluidSystem::gasPvt().inverseFormationVolumeFactor(fs.pvtRegionIndex(), temperature, p_avg, rv);
                                }
                                else {
                                    b_perf[gaspos] = FluidSystem::gasPvt().saturatedInverseFormationVolumeFactor(fs.pvtRegionIndex(), temperature, p_avg);
                                }

                            } else {
                                b_perf[gaspos] = FluidSystem::gasPvt().saturatedInverseFormationVolumeFactor(fs.pvtRegionIndex(), temperature, p_avg);
                            }
                        }

                        if (pu.phase_used[BlackoilPhases::Liquid]) {
                            int oilpos = pu.phase_pos[BlackoilPhases::Liquid] + perf * pu.num_phases;
                            int oilpos_well = pu.phase_pos[BlackoilPhases::Liquid] + w * pu.num_phases;
                            if (pu.phase_used[BlackoilPhases::Vapour]) {
                                rsmax_perf[perf] = FluidSystem::oilPvt().saturatedGasDissolutionFactor(fs.pvtRegionIndex(), temperature, p_avg);
                                int gaspos_well = pu.phase_pos[BlackoilPhases::Vapour] + w * pu.num_phases;
                                const double gasrate = std::abs(xw.wellRates()[gaspos_well]);
                                if (gasrate > 0) {
                                    const double oilrate = std::abs(xw.wellRates()[oilpos_well]);
                                    double rs = 0.0;
                                    if (oilrate > 0) {
                                        rs = gasrate / oilrate;
                                    }
                                    rs = std::min(rs, rsmax_perf[perf]);
                                    b_perf[oilpos] = FluidSystem::oilPvt().inverseFormationVolumeFactor(fs.pvtRegionIndex(), temperature, p_avg, rs);
                                } else {
                                    b_perf[oilpos] = FluidSystem::oilPvt().saturatedInverseFormationVolumeFactor(fs.pvtRegionIndex(), temperature, p_avg);
                                }
                            } else {
                                b_perf[oilpos] = FluidSystem::oilPvt().saturatedInverseFormationVolumeFactor(fs.pvtRegionIndex(), temperature, p_avg);
                            }
                        }

                        // Surface density.
                        for (int p = 0; p < pu.num_phases; ++p) {
                            surf_dens_perf[np*perf + p] = FluidSystem::referenceDensity( flowPhaseToEbosPhaseIdx( p ), fs.pvtRegionIndex());
                        }
                    }
                }
            }

            template <class WellState>
            void updateWellState(const BVector& dwells,
                                 WellState& well_state)
            {
                if( localWellsActive() )
                {
                    const int np = wells().number_of_phases;
                    const int nw = wells().number_of_wells;

                    double dFLimit = dWellFractionMax();
                    double dBHPLimit = dbhpMaxRel();
                    std::vector<double> xvar_well_old = well_state.wellSolutions();

                    for (int w = 0; w < nw; ++w) {

                        // update the second and third well variable (The flux fractions)
                        std::vector<double> F(np,0.0);
                        if (active_[ Water ]) {
                            const int sign2 = dwells[w][flowPhaseToEbosCompIdx(WFrac)] > 0 ? 1: -1;
                            const double dx2_limited = sign2 * std::min(std::abs(dwells[w][flowPhaseToEbosCompIdx(WFrac)]),dFLimit);
                            well_state.wellSolutions()[WFrac*nw + w] = xvar_well_old[WFrac*nw + w] - dx2_limited;
                        }

                        if (active_[ Gas ]) {
                            const int sign3 = dwells[w][flowPhaseToEbosCompIdx(GFrac)] > 0 ? 1: -1;
                            const double dx3_limited = sign3 * std::min(std::abs(dwells[w][flowPhaseToEbosCompIdx(GFrac)]),dFLimit);
                            well_state.wellSolutions()[GFrac*nw + w] = xvar_well_old[GFrac*nw + w] - dx3_limited;
                        }

                        assert(active_[ Oil ]);
                        F[Oil] = 1.0;
                        if (active_[ Water ]) {
                            F[Water] = well_state.wellSolutions()[WFrac*nw + w];
                            F[Oil] -= F[Water];
                        }

                        if (active_[ Gas ]) {
                            F[Gas] = well_state.wellSolutions()[GFrac*nw + w];
                            F[Oil] -= F[Gas];
                        }

                        if (active_[ Water ]) {
                            if (F[Water] < 0.0) {
                                if (active_[ Gas ]) {
                                    F[Gas] /= (1.0 - F[Water]);
                                }
                                F[Oil] /= (1.0 - F[Water]);
                                F[Water] = 0.0;
                            }
                        }
                        if (active_[ Gas ]) {
                            if (F[Gas] < 0.0) {
                                if (active_[ Water ]) {
                                    F[Water] /= (1.0 - F[Gas]);
                                }
                                F[Oil] /= (1.0 - F[Gas]);
                                F[Gas] = 0.0;
                            }
                        }
                        if (F[Oil] < 0.0) {
                            if (active_[ Water ]) {
                                F[Water] /= (1.0 - F[Oil]);
                            }
                            if (active_[ Gas ]) {
                                F[Gas] /= (1.0 - F[Oil]);
                            }
                            F[Oil] = 0.0;
                        }

                        if (active_[ Water ]) {
                            well_state.wellSolutions()[WFrac*nw + w] = F[Water];
                        }
                        if (active_[ Gas ]) {
                            well_state.wellSolutions()[GFrac*nw + w] = F[Gas];
                        }

                        // The interpretation of the first well variable depends on the well control
                        const WellControls* wc = wells().ctrls[w];

                        // The current control in the well state overrides
                        // the current control set in the Wells struct, which
                        // is instead treated as a default.
                        const int current = well_state.currentControls()[w];
                        const double target_rate = well_controls_iget_target(wc, current);

                        std::vector<double> g = {1,1,0.01};
                        if (well_controls_iget_type(wc, current) == RESERVOIR_RATE) {
                            const double* distr = well_controls_iget_distr(wc, current);
                            for (int p = 0; p < np; ++p) {
                                F[p] /= distr[p];
                            }
                        } else {
                            for (int p = 0; p < np; ++p) {
                                F[p] /= g[p];
                            }
                        }

                        switch (well_controls_iget_type(wc, current)) {
                        case THP: // The BHP and THP both uses the total rate as first well variable.
                        case BHP:
                        {
                            well_state.wellSolutions()[nw*XvarWell + w] = xvar_well_old[nw*XvarWell + w] - dwells[w][flowPhaseToEbosCompIdx(XvarWell)];

                            switch (wells().type[w]) {
                            case INJECTOR:
                                for (int p = 0; p < np; ++p) {
                                    const double comp_frac = wells().comp_frac[np*w + p];
                                    well_state.wellRates()[w*np + p] = comp_frac * well_state.wellSolutions()[nw*XvarWell + w];
                                }
                                break;
                            case PRODUCER:
                                for (int p = 0; p < np; ++p) {
                                    well_state.wellRates()[w*np + p] = well_state.wellSolutions()[nw*XvarWell + w] * F[p];
                                }
                                break;
                            }

                            if (well_controls_iget_type(wc, current) == THP) {

                                // Calculate bhp from thp control and well rates
                                double aqua = 0.0;
                                double liquid = 0.0;
                                double vapour = 0.0;

                                const Opm::PhaseUsage& pu = phase_usage_;

                                if (active_[ Water ]) {
                                    aqua = well_state.wellRates()[w*np + pu.phase_pos[ Water ] ];
                                }
                                if (active_[ Oil ]) {
                                    liquid = well_state.wellRates()[w*np + pu.phase_pos[ Oil ] ];
                                }
                                if (active_[ Gas ]) {
                                    vapour = well_state.wellRates()[w*np + pu.phase_pos[ Gas ] ];
                                }

                                const int vfp        = well_controls_iget_vfp(wc, current);
                                const double& thp    = well_controls_iget_target(wc, current);
                                const double& alq    = well_controls_iget_alq(wc, current);

                                //Set *BHP* target by calculating bhp from THP
                                const WellType& well_type = wells().type[w];
                                // pick the density in the top layer
                                const int perf = wells().well_connpos[w];
                                const double rho = well_perforation_densities_[perf];

                                if (well_type == INJECTOR) {
                                    double dp = wellhelpers::computeHydrostaticCorrection(
                                                wells(), w, vfp_properties_->getInj()->getTable(vfp)->getDatumDepth(),
                                                rho, gravity_);

                                    well_state.bhp()[w] = vfp_properties_->getInj()->bhp(vfp, aqua, liquid, vapour, thp) - dp;
                                }
                                else if (well_type == PRODUCER) {
                                    double dp = wellhelpers::computeHydrostaticCorrection(
                                                wells(), w, vfp_properties_->getProd()->getTable(vfp)->getDatumDepth(),
                                                rho, gravity_);

                                    well_state.bhp()[w] = vfp_properties_->getProd()->bhp(vfp, aqua, liquid, vapour, thp, alq) - dp;
                                }
                                else {
                                    OPM_THROW(std::logic_error, "Expected INJECTOR or PRODUCER well");
                                }
                            }

                        }
                            break;
                        case SURFACE_RATE: // Both rate controls use bhp as first well variable
                        case RESERVOIR_RATE:
                        {
                            const int sign1 = dwells[w][flowPhaseToEbosCompIdx(XvarWell)] > 0 ? 1: -1;
                            const double dx1_limited = sign1 * std::min(std::abs(dwells[w][flowPhaseToEbosCompIdx(XvarWell)]),std::abs(xvar_well_old[nw*XvarWell + w])*dBHPLimit);
                            well_state.wellSolutions()[nw*XvarWell + w] = std::max(xvar_well_old[nw*XvarWell + w] - dx1_limited,1e5);
                            well_state.bhp()[w] = well_state.wellSolutions()[nw*XvarWell + w];

                            if (well_controls_iget_type(wc, current) == SURFACE_RATE) {
                                if (wells().type[w]==PRODUCER) {

                                    const double* distr = well_controls_iget_distr(wc, current);

                                    double F_target = 0.0;
                                    for (int p = 0; p < np; ++p) {
                                        F_target += distr[p] * F[p];
                                    }
                                    for (int p = 0; p < np; ++p) {
                                        well_state.wellRates()[np*w + p] = F[p] * target_rate / F_target;
                                    }
                                } else {

                                    for (int p = 0; p < np; ++p) {
                                        well_state.wellRates()[w*np + p] = wells().comp_frac[np*w + p] * target_rate;
                                    }
                                }
                            } else { // RESERVOIR_RATE
                                for (int p = 0; p < np; ++p) {
                                    well_state.wellRates()[np*w + p] = F[p] * target_rate;
                                }
                            }
                        }
                            break;
                        }
                    }
                }
            }



            template <class WellState>
            void updateWellControls(WellState& xw)
            {
                if( !localWellsActive() ) return ;


                const int np = wells().number_of_phases;
                const int nw = wells().number_of_wells;

                // keeping a copy of the current controls, to see whether control changes later.
                std::vector<int> old_control_index(nw, 0);
                for (int w = 0; w < nw; ++w) {
                    old_control_index[w] = xw.currentControls()[w];
                }

                // Find, for each well, if any constraints are broken. If so,
                // switch control to first broken constraint.
        #pragma omp parallel for schedule(dynamic)
                for (int w = 0; w < nw; ++w) {
                    WellControls* wc = wells().ctrls[w];
                    // The current control in the well state overrides
                    // the current control set in the Wells struct, which
                    // is instead treated as a default.
                    int current = xw.currentControls()[w];
                    // Loop over all controls except the current one, and also
                    // skip any RESERVOIR_RATE controls, since we cannot
                    // handle those.
                    const int nwc = well_controls_get_num(wc);
                    int ctrl_index = 0;
                    for (; ctrl_index < nwc; ++ctrl_index) {
                        if (ctrl_index == current) {
                            // This is the currently used control, so it is
                            // used as an equation. So this is not used as an
                            // inequality constraint, and therefore skipped.
                            continue;
                        }
                        if (wellhelpers::constraintBroken(
                                xw.bhp(), xw.thp(), xw.wellRates(),
                                w, np, wells().type[w], wc, ctrl_index)) {
                            // ctrl_index will be the index of the broken constraint after the loop.
                            break;
                        }
                    }
                    if (ctrl_index != nwc) {
                        // Constraint number ctrl_index was broken, switch to it.
                        xw.currentControls()[w] = ctrl_index;
                        current = xw.currentControls()[w];
                        well_controls_set_current( wc, current);
                    }

                    // update whether well is under group control
                    if (wellCollection()->groupControlActive()) {
                        // get well node in the well collection
                        WellNode& well_node = well_collection_->findWellNode(std::string(wells().name[w]));

                        // update whehter the well is under group control or individual control
                        if (well_node.groupControlIndex() >= 0 && current == well_node.groupControlIndex()) {
                            // under group control
                            well_node.setIndividualControl(false);
                        } else {
                            // individual control
                            well_node.setIndividualControl(true);
                        }
                    }
                }

                // upate the well targets following group controls
                if (wellCollection()->groupControlActive()) {
                    applyVREPGroupControl(xw);
                    wellCollection()->updateWellTargets(xw.wellRates());
                }

                // the new well control indices after all the related updates,
                std::vector<int> updated_control_index(nw, 0);
                for (int w = 0; w < nw; ++w) {
                    updated_control_index[w] = xw.currentControls()[w];
                }

                // checking whether control changed
                wellhelpers::WellSwitchingLogger logger;
                for (int w = 0; w < nw; ++w) {
                    if (updated_control_index[w] != old_control_index[w]) {
                        WellControls* wc = wells().ctrls[w];
                        logger.wellSwitched(wells().name[w],
                                            well_controls_iget_type(wc, old_control_index[w]),
                                            well_controls_iget_type(wc, updated_control_index[w]));
                        updateWellStateWithTarget(wc, updated_control_index[w], w, xw);
                    }
                }
            }


            int flowPhaseToEbosPhaseIdx( const int phaseIdx ) const
            {
                const int flowToEbos[ 3 ] = { FluidSystem::waterPhaseIdx, FluidSystem::oilPhaseIdx, FluidSystem::gasPhaseIdx };
                return flowToEbos[ phaseIdx ];
            }

            /// upate the dynamic lists related to economic limits
            template<class WellState>
            void
            updateListEconLimited(const Schedule& schedule,
                                  const int current_step,
                                  const Wells* wells_struct,
                                  const WellState& well_state,
                                  DynamicListEconLimited& list_econ_limited) const
            {
                // With no wells (on process) wells_struct is a null pointer
                const int nw = (wells_struct)? wells_struct->number_of_wells : 0;

                for (int w = 0; w < nw; ++w) {
                    // flag to check if the mim oil/gas rate limit is violated
                    bool rate_limit_violated = false;
                    const std::string& well_name = wells_struct->name[w];
                    const Well* well_ecl = schedule.getWell(well_name);
                    const WellEconProductionLimits& econ_production_limits = well_ecl->getEconProductionLimits(current_step);

                    // economic limits only apply for production wells.
                    if (wells_struct->type[w] != PRODUCER) {
                        continue;
                    }

                    // if no limit is effective here, then continue to the next well
                    if ( !econ_production_limits.onAnyEffectiveLimit() ) {
                        continue;
                    }
                    // for the moment, we only handle rate limits, not handling potential limits
                    // the potential limits should not be difficult to add
                    const WellEcon::QuantityLimitEnum& quantity_limit = econ_production_limits.quantityLimit();
                    if (quantity_limit == WellEcon::POTN) {
                        const std::string msg = std::string("POTN limit for well ") + well_name + std::string(" is not supported for the moment. \n")
                                              + std::string("All the limits will be evaluated based on RATE. ");
                        OpmLog::warning("NOT_SUPPORTING_POTN", msg);
                    }

                    const WellMapType& well_map = well_state.wellMap();
                    const typename WellMapType::const_iterator i_well = well_map.find(well_name);
                    assert(i_well != well_map.end()); // should always be found?
                    const WellMapEntryType& map_entry = i_well->second;
                    const int well_number = map_entry[0];

                    if (econ_production_limits.onAnyRateLimit()) {
                        rate_limit_violated = checkRateEconLimits(econ_production_limits, well_state, well_number);
                    }

                    if (rate_limit_violated) {
                        if (econ_production_limits.endRun()) {
                            const std::string warning_message = std::string("ending run after well closed due to economic limits is not supported yet \n")
                                                              + std::string("the program will keep running after ") + well_name + std::string(" is closed");
                            OpmLog::warning("NOT_SUPPORTING_ENDRUN", warning_message);
                        }

                        if (econ_production_limits.validFollowonWell()) {
                            OpmLog::warning("NOT_SUPPORTING_FOLLOWONWELL", "opening following on well after well closed is not supported yet");
                        }

                        if (well_ecl->getAutomaticShutIn()) {
                            list_econ_limited.addShutWell(well_name);
                            const std::string msg = std::string("well ") + well_name + std::string(" will be shut in due to economic limit");
                            OpmLog::info(msg);
                        } else {
                            list_econ_limited.addStoppedWell(well_name);
                            const std::string msg = std::string("well ") + well_name + std::string(" will be stopped due to economic limit");
                            OpmLog::info(msg);
                        }
                        // the well is closed, not need to check other limits
                        continue;
                    }

                    // checking for ratio related limits, mostly all kinds of ratio.
                    bool ratio_limits_violated = false;
                    RatioCheckTuple ratio_check_return;

                    if (econ_production_limits.onAnyRatioLimit()) {
                        ratio_check_return = checkRatioEconLimits(econ_production_limits, well_state, map_entry);
                        ratio_limits_violated = std::get<0>(ratio_check_return);
                    }

                    if (ratio_limits_violated) {
                        const bool last_connection = std::get<1>(ratio_check_return);
                        const int worst_offending_connection = std::get<2>(ratio_check_return);

                        const int perf_start = map_entry[1];

                        assert((worst_offending_connection >= 0) && (worst_offending_connection <  map_entry[2]));

                        const int cell_worst_offending_connection = wells_struct->well_cells[perf_start + worst_offending_connection];
                        list_econ_limited.addClosedConnectionsForWell(well_name, cell_worst_offending_connection);
                        const std::string msg = std::string("Connection ") + std::to_string(worst_offending_connection) + std::string(" for well ")
                                              + well_name + std::string(" will be closed due to economic limit");
                        OpmLog::info(msg);

                        if (last_connection) {
                            list_econ_limited.addShutWell(well_name);
                            const std::string msg2 = well_name + std::string(" will be shut due to the last connection closed");
                            OpmLog::info(msg2);
                        }
                    }

                }
            }

            template <class WellState>
            void computeWellConnectionDensitesPressures(const WellState& xw,
                                                        const std::vector<double>& b_perf,
                                                        const std::vector<double>& rsmax_perf,
                                                        const std::vector<double>& rvmax_perf,
                                                        const std::vector<double>& surf_dens_perf,
                                                        const std::vector<double>& depth_perf,
                                                        const double grav) {
                // Compute densities
                well_perforation_densities_ =
                        WellDensitySegmented::computeConnectionDensities(
                                wells(), xw, phase_usage_,
                                b_perf, rsmax_perf, rvmax_perf, surf_dens_perf);

                // Compute pressure deltas
                well_perforation_pressure_diffs_ =
                        WellDensitySegmented::computeConnectionPressureDelta(
                                wells(), depth_perf, well_perforation_densities_, grav);



            }





            // TODO: Later we might want to change the function to only handle one well,
            // the requirement for well potential calculation can be based on individual wells.
            // getBhp() will be refactored to reduce the duplication of the code calculating the bhp from THP.
            template<typename Simulator>
            void
            computeWellPotentials(const Simulator& ebosSimulator,
                                  WellState& well_state)  const
            {

                // number of wells and phases
                const int nw = wells().number_of_wells;
                const int np = wells().number_of_phases;

                for (int w = 0; w < nw; ++w) {
                    // bhp needs to be determined for the well potential calculation
                    double bhp = 0.;

                    const WellControls* well_control = wells().ctrls[w];
                    // The number of the well controls
                    const int nwc = well_controls_get_num(well_control);

                    // Finding a BHP control or a THP control
                    // IF we find a THP control, we calculate the BHP value.
                    // TODO: there is option to ignore the THP limit when calculating well potentials,
                    // we are not handling it for the moment.
                    for (int ctrl_index = 0; ctrl_index < nwc; ++ctrl_index) {
                        if (well_controls_iget_type(well_control, ctrl_index) == BHP) {
                            // set bhp to the bhp value
                            bhp = well_controls_iget_target(well_control, ctrl_index);
                        }


                        if (well_controls_iget_type(well_control, ctrl_index) == THP) {
                            double aqua = 0.0;
                            double liquid = 0.0;
                            double vapour = 0.0;

                            const Opm::PhaseUsage& pu = phase_usage_;

                            if (active_[ Water ]) {
                                aqua = well_state.wellRates()[w*np + pu.phase_pos[ Water ] ];
                            }
                            if (active_[ Oil ]) {
                                liquid = well_state.wellRates()[w*np + pu.phase_pos[ Oil ] ];
                            }
                            if (active_[ Gas ]) {
                                vapour = well_state.wellRates()[w*np + pu.phase_pos[ Gas ] ];
                            }

                            const int vfp        = well_controls_iget_vfp(well_control, ctrl_index);
                            const double& thp    = well_controls_iget_target(well_control, ctrl_index);
                            const double& alq    = well_controls_iget_alq(well_control, ctrl_index);

                            // Calculating the BHP value based on THP
                            const WellType& well_type = wells().type[w];
                            const int first_perf = wells().well_connpos[w]; //first perforation

                            if (well_type == INJECTOR) {
                                const double dp = wellhelpers::computeHydrostaticCorrection(
                                                  wells(), w, vfp_properties_->getInj()->getTable(vfp)->getDatumDepth(),
                                                  wellPerforationDensities()[first_perf], gravity_);
                                const double bhp_calculated = vfp_properties_->getInj()->bhp(vfp, aqua, liquid, vapour, thp) - dp;
                                // apply the strictest of the bhp controlls i.e. smallest bhp for injectors
                                if (bhp_calculated < bhp) {
                                    bhp = bhp_calculated;
                                }
                            }
                            else if (well_type == PRODUCER) {
                                const double dp = wellhelpers::computeHydrostaticCorrection(
                                                  wells(), w, vfp_properties_->getProd()->getTable(vfp)->getDatumDepth(),
                                                  wellPerforationDensities()[first_perf], gravity_);
                                const double bhp_calculated = vfp_properties_->getProd()->bhp(vfp, aqua, liquid, vapour, thp, alq) - dp;
                                // apply the strictest of the bhp controlls i.e. largest bhp for producers
                                if (bhp_calculated > bhp) {
                                    bhp = bhp_calculated;
                                }
                            } else {
                                OPM_THROW(std::logic_error, "Expected PRODUCER or INJECTOR type of well");
                            }
                        }
                    }

                    assert(bhp != 0.0);

                    // Should we consider crossflow when calculating well potentionals?
                    const bool allow_cf = allow_cross_flow(w, ebosSimulator);
                    for (int perf = wells().well_connpos[w]; perf < wells().well_connpos[w+1]; ++perf) {
                        const int cell_index = wells().well_cells[perf];
                        const auto& intQuants = *(ebosSimulator.model().cachedIntensiveQuantities(cell_index, /*timeIdx=*/ 0));
                        std::vector<EvalWell> well_potentials(np, 0.0);
                        computeWellFlux(w, wells().WI[perf], intQuants, bhp, wellPerforationPressureDiffs()[perf], allow_cf, well_potentials);
                        for(int p = 0; p < np; ++p) {
                            well_state.wellPotentials()[perf * np + p] = well_potentials[p].value();
                        }
                    }
                }
            }





            WellCollection* wellCollection() const
            {
                return well_collection_;
            }





            const std::vector<double>&
            wellPerfEfficiencyFactors() const
            {
                return well_perforation_efficiency_factors_;
            }





            void calculateEfficiencyFactors()
            {
                if ( !localWellsActive() ) {
                    return;
                }

                const int nw = wells().number_of_wells;

                for (int w = 0; w < nw; ++w) {
                    const std::string well_name = wells().name[w];
                    const WellNode& well_node = wellCollection()->findWellNode(well_name);

                    const double well_efficiency_factor = well_node.getAccumulativeEfficiencyFactor();

                    // assign the efficiency factor to each perforation related.
                    for (int perf = wells().well_connpos[w]; perf < wells().well_connpos[w + 1]; ++perf) {
                        well_perforation_efficiency_factors_[perf] = well_efficiency_factor;
                    }
                }
            }




            void computeWellVoidageRates(const WellState& well_state,
                                         std::vector<double>& well_voidage_rates,
                                         std::vector<double>& voidage_conversion_coeffs) const
            {
                if ( !localWellsActive() ) {
                    return;
                }
                // TODO: for now, we store the voidage rates for all the production wells.
                // For injection wells, the rates are stored as zero.
                // We only store the conversion coefficients for all the injection wells.
                // Later, more delicate model will be implemented here.
                // And for the moment, group control can only work for serial running.
                const int nw = well_state.numWells();
                const int np = well_state.numPhases();

                // we calculate the voidage rate for each well, that means the sum of all the phases.
                well_voidage_rates.resize(nw, 0);
                // store the conversion coefficients, while only for the use of injection wells.
                voidage_conversion_coeffs.resize(nw * np, 1.0);

                std::vector<double> well_rates(np, 0.0);
                std::vector<double> convert_coeff(np, 1.0);

                for (int w = 0; w < nw; ++w) {
                    const bool is_producer = wells().type[w] == PRODUCER;

                    // not sure necessary to change all the value to be positive
                    if (is_producer) {
                        std::transform(well_state.wellRates().begin() + np * w,
                                       well_state.wellRates().begin() + np * (w + 1),
                                       well_rates.begin(), std::negate<double>());

                        // the average hydrocarbon conditions of the whole field will be used
                        const int fipreg = 0; // Not considering FIP for the moment.

                        rate_converter_->calcCoeff(well_rates, fipreg, convert_coeff);
                        well_voidage_rates[w] = std::inner_product(well_rates.begin(), well_rates.end(),
                                                                   convert_coeff.begin(), 0.0);
                    } else {
                        // TODO: Not sure whether will encounter situation with all zero rates
                        // and whether it will cause problem here.
                        std::copy(well_state.wellRates().begin() + np * w,
                                  well_state.wellRates().begin() + np * (w + 1),
                                  well_rates.begin());
                        // the average hydrocarbon conditions of the whole field will be used
                        const int fipreg = 0; // Not considering FIP for the moment.
                        rate_converter_->calcCoeff(well_rates, fipreg, convert_coeff);
                        std::copy(convert_coeff.begin(), convert_coeff.end(),
                                  voidage_conversion_coeffs.begin() + np * w);
                    }
                }
            }





            void applyVREPGroupControl(WellState& well_state) const
            {
                if ( wellCollection()->havingVREPGroups() ) {
                    std::vector<double> well_voidage_rates;
                    std::vector<double> voidage_conversion_coeffs;
                    computeWellVoidageRates(well_state, well_voidage_rates, voidage_conversion_coeffs);
                    wellCollection()->applyVREPGroupControls(well_voidage_rates, voidage_conversion_coeffs);

                    // for the wells under group control, update the currentControls for the well_state
                    for (const WellNode* well_node : wellCollection()->getLeafNodes()) {
                        if (well_node->isInjector() && !well_node->individualControl()) {
                            const int well_index = well_node->selfIndex();
                            well_state.currentControls()[well_index] = well_node->groupControlIndex();
                        }
                    }
                }
            }



        protected:
            bool wells_active_;
            const Wells*   wells_;

            // Well collection is used to enforce the group control
            WellCollection* well_collection_;

            ModelParameters param_;
            bool terminal_output_;

            PhaseUsage phase_usage_;
            std::vector<bool>  active_;
            const VFPProperties* vfp_properties_;
            double gravity_;
            const RateConverterType* rate_converter_;

            // The efficiency factor for each connection. It is specified based on wells and groups,
            // We calculate the factor for each connection for the computation of contributions to the mass balance equations.
            // By default, they should all be one.
            std::vector<double> well_perforation_efficiency_factors_;
            // the depth of the all the cell centers
            // for standard Wells, it the same with the perforation depth
            std::vector<double> cell_depths_;
            std::vector<double> pv_;

            std::vector<double> well_perforation_densities_;
            std::vector<double> well_perforation_pressure_diffs_;

            std::vector<EvalWell> wellVariables_;
            std::vector<double> F0_;

            Mat duneB_;
            Mat duneC_;
            Mat invDuneD_;

            BVector resWell_;

            mutable BVector Cx_;
            mutable BVector invDrw_;
            mutable BVector scaleAddRes_;

            double dbhpMaxRel() const {return param_.dbhp_max_rel_; }
            double dWellFractionMax() const {return param_.dwell_fraction_max_; }

            // protected methods
            EvalWell getBhp(const int wellIdx) const {
                const WellControls* wc = wells().ctrls[wellIdx];
                if (well_controls_get_current_type(wc) == BHP) {
                    EvalWell bhp = 0.0;
                    const double target_rate = well_controls_get_current_target(wc);
                    bhp.setValue(target_rate);
                    return bhp;
                } else if (well_controls_get_current_type(wc) == THP) {
                    const int control = well_controls_get_current(wc);
                    const double thp = well_controls_get_current_target(wc);
                    const double alq = well_controls_iget_alq(wc, control);
                    const int table_id = well_controls_iget_vfp(wc, control);
                    EvalWell aqua = 0.0;
                    EvalWell liquid = 0.0;
                    EvalWell vapour = 0.0;
                    EvalWell bhp = 0.0;
                    double vfp_ref_depth = 0.0;

                    const Opm::PhaseUsage& pu = phase_usage_;

                    if (active_[ Water ]) {
                        aqua = getQs(wellIdx, pu.phase_pos[ Water]);
                    }
                    if (active_[ Oil ]) {
                        liquid = getQs(wellIdx, pu.phase_pos[ Oil ]);
                    }
                    if (active_[ Gas ]) {
                        vapour = getQs(wellIdx, pu.phase_pos[ Gas ]);
                    }
                    if (wells().type[wellIdx] == INJECTOR) {
                        bhp = vfp_properties_->getInj()->bhp(table_id, aqua, liquid, vapour, thp);
                        vfp_ref_depth = vfp_properties_->getInj()->getTable(table_id)->getDatumDepth();
                    } else {
                        bhp = vfp_properties_->getProd()->bhp(table_id, aqua, liquid, vapour, thp, alq);
                        vfp_ref_depth = vfp_properties_->getProd()->getTable(table_id)->getDatumDepth();
                    }

                    // pick the density in the top layer
                    const int perf = wells().well_connpos[wellIdx];
                    const double rho = well_perforation_densities_[perf];
                    const double dp = wellhelpers::computeHydrostaticCorrection(wells(), wellIdx, vfp_ref_depth, rho, gravity_);
                    bhp -= dp;
                    return bhp;

                }
                const int nw = wells().number_of_wells;
                return wellVariables_[nw*XvarWell + wellIdx];
            }

            EvalWell getQs(const int wellIdx, const int phaseIdx) const {
                EvalWell qs = 0.0;
                const WellControls* wc = wells().ctrls[wellIdx];
                const int np = wells().number_of_phases;
                const int nw = wells().number_of_wells;
                const double target_rate = well_controls_get_current_target(wc);

                if (wells().type[wellIdx] == INJECTOR) {
                    const double comp_frac = wells().comp_frac[np*wellIdx + phaseIdx];
                    if (comp_frac == 0.0)
                        return qs;

                    if (well_controls_get_current_type(wc) == BHP || well_controls_get_current_type(wc) == THP) {
                        return wellVariables_[nw*XvarWell + wellIdx];
                    }
                    qs.setValue(target_rate);
                    return qs;
                }

                // Producers
                if (well_controls_get_current_type(wc) == BHP || well_controls_get_current_type(wc) == THP ) {
                    return wellVariables_[nw*XvarWell + wellIdx] * wellVolumeFractionScaled(wellIdx,phaseIdx);
                }

                if (well_controls_get_current_type(wc) == SURFACE_RATE) {
                    // checking how many phases are included in the rate control
                    // to decide wheter it is a single phase rate control or not
                    const double* distr = well_controls_get_current_distr(wc);
                    int num_phases_under_rate_control = 0;
                    for (int phase = 0; phase < np; ++phase) {
                        if (distr[phase] > 0.0) {
                            num_phases_under_rate_control += 1;
                        }
                    }

                    // there should be at least one phase involved
                    assert(num_phases_under_rate_control > 0);

                    // when it is a single phase rate limit
                    if (num_phases_under_rate_control == 1) {
                        if (distr[phaseIdx] == 1.0) {
                            qs.setValue(target_rate);
                            return qs;
                        }

                        int currentControlIdx = 0;
                        for (int i = 0; i < np; ++i) {
                            currentControlIdx += wells().comp_frac[np*wellIdx + i] * i;
                        }

                        const double eps = 1e-6;
                        if (wellVolumeFractionScaled(wellIdx,currentControlIdx) < eps) {
                            return qs;
                        }
                        return (target_rate * wellVolumeFractionScaled(wellIdx,phaseIdx) / wellVolumeFractionScaled(wellIdx,currentControlIdx));
                    }

                    // when it is a combined two phase rate limit, such like LRAT
                    // we neec to calculate the rate for the certain phase
                    if (num_phases_under_rate_control == 2) {
                        EvalWell combined_volume_fraction = 0.;
                        for (int p = 0; p < np; ++p) {
                            if (distr[p] == 1.0) {
                                combined_volume_fraction += wellVolumeFractionScaled(wellIdx, p);
                            }
                        }
                        return (target_rate * wellVolumeFractionScaled(wellIdx,phaseIdx) / combined_volume_fraction);
                    }

                    // suppose three phase combined limit is the same with RESV
                    // not tested yet.
                }
                // ReservoirRate
                return target_rate * wellVolumeFractionScaled(wellIdx,phaseIdx);
            }

            EvalWell wellVolumeFraction(const int wellIdx, const int phaseIdx) const {
                const int nw = wells().number_of_wells;
                if (phaseIdx == Water) {
                   return wellVariables_[WFrac * nw + wellIdx];
                }

                if (phaseIdx == Gas) {
                   return wellVariables_[GFrac * nw + wellIdx];
                }

                // Oil fraction
                EvalWell well_fraction = 1.0;
                if (active_[Water]) {
                    well_fraction -= wellVariables_[WFrac * nw + wellIdx];
                }

                if (active_[Gas]) {
                    well_fraction -= wellVariables_[GFrac * nw + wellIdx];
                }
                return well_fraction;
            }

            EvalWell wellVolumeFractionScaled(const int wellIdx, const int phaseIdx) const {
                const WellControls* wc = wells().ctrls[wellIdx];
                if (well_controls_get_current_type(wc) == RESERVOIR_RATE) {
                    const double* distr = well_controls_get_current_distr(wc);
                    return wellVolumeFraction(wellIdx, phaseIdx) / distr[phaseIdx];
                }
                std::vector<double> g = {1,1,0.01};
                return (wellVolumeFraction(wellIdx, phaseIdx) / g[phaseIdx]);
            }



            template <class WellState>
            bool checkRateEconLimits(const WellEconProductionLimits& econ_production_limits,
                                     const WellState& well_state,
                                     const int well_number) const
            {
                const Opm::PhaseUsage& pu = phase_usage_;
                const int np = well_state.numPhases();

                if (econ_production_limits.onMinOilRate()) {
                    assert(active_[Oil]);
                    const double oil_rate = well_state.wellRates()[well_number * np + pu.phase_pos[ Oil ] ];
                    const double min_oil_rate = econ_production_limits.minOilRate();
                    if (std::abs(oil_rate) < min_oil_rate) {
                        return true;
                    }
                }

                if (econ_production_limits.onMinGasRate() ) {
                    assert(active_[Gas]);
                    const double gas_rate = well_state.wellRates()[well_number * np + pu.phase_pos[ Gas ] ];
                    const double min_gas_rate = econ_production_limits.minGasRate();
                    if (std::abs(gas_rate) < min_gas_rate) {
                        return true;
                    }
                }

                if (econ_production_limits.onMinLiquidRate() ) {
                    assert(active_[Oil]);
                    assert(active_[Water]);
                    const double oil_rate = well_state.wellRates()[well_number * np + pu.phase_pos[ Oil ] ];
                    const double water_rate = well_state.wellRates()[well_number * np + pu.phase_pos[ Water ] ];
                    const double liquid_rate = oil_rate + water_rate;
                    const double min_liquid_rate = econ_production_limits.minLiquidRate();
                    if (std::abs(liquid_rate) < min_liquid_rate) {
                        return true;
                    }
                }

                if (econ_production_limits.onMinReservoirFluidRate()) {
                    OpmLog::warning("NOT_SUPPORTING_MIN_RESERVOIR_FLUID_RATE", "Minimum reservoir fluid production rate limit is not supported yet");
                }

                return false;
            }


            using WellMapType = typename WellState::WellMapType;
            using WellMapEntryType = typename WellState::mapentry_t;

            // a tuple type for ratio limit check.
            // first value indicates whether ratio limit is violated, when the ratio limit is not violated, the following three
            // values should not be used.
            // second value indicates whehter there is only one connection left.
            // third value indicates the indx of the worst-offending connection.
            // the last value indicates the extent of the violation for the worst-offending connection, which is defined by
            // the ratio of the actual value to the value of the violated limit.
            using RatioCheckTuple = std::tuple<bool, bool, int, double>;

            enum ConnectionIndex {
                INVALIDCONNECTION = -10000
            };


            template <class WellState>
            RatioCheckTuple checkRatioEconLimits(const WellEconProductionLimits& econ_production_limits,
                                                 const WellState& well_state,
                                                 const WellMapEntryType& map_entry) const
            {
                // TODO: not sure how to define the worst-offending connection when more than one
                //       ratio related limit is violated.
                //       The defintion used here is that we define the violation extent based on the
                //       ratio between the value and the corresponding limit.
                //       For each violated limit, we decide the worst-offending connection separately.
                //       Among the worst-offending connections, we use the one has the biggest violation
                //       extent.

                bool any_limit_violated = false;
                bool last_connection = false;
                int worst_offending_connection = INVALIDCONNECTION;
                double violation_extent = -1.0;

                if (econ_production_limits.onMaxWaterCut()) {
                    const RatioCheckTuple water_cut_return = checkMaxWaterCutLimit(econ_production_limits, well_state, map_entry);
                    bool water_cut_violated = std::get<0>(water_cut_return);
                    if (water_cut_violated) {
                        any_limit_violated = true;
                        const double violation_extent_water_cut = std::get<3>(water_cut_return);
                        if (violation_extent_water_cut > violation_extent) {
                            violation_extent = violation_extent_water_cut;
                            worst_offending_connection = std::get<2>(water_cut_return);
                            last_connection = std::get<1>(water_cut_return);
                        }
                    }
                }

                if (econ_production_limits.onMaxGasOilRatio()) {
                    OpmLog::warning("NOT_SUPPORTING_MAX_GOR", "the support for max Gas-Oil ratio is not implemented yet!");
                }

                if (econ_production_limits.onMaxWaterGasRatio()) {
                    OpmLog::warning("NOT_SUPPORTING_MAX_WGR", "the support for max Water-Gas ratio is not implemented yet!");
                }

                if (econ_production_limits.onMaxGasLiquidRatio()) {
                    OpmLog::warning("NOT_SUPPORTING_MAX_GLR", "the support for max Gas-Liquid ratio is not implemented yet!");
                }

                if (any_limit_violated) {
                    assert(worst_offending_connection >=0);
                    assert(violation_extent > 1.);
                }

                return std::make_tuple(any_limit_violated, last_connection, worst_offending_connection, violation_extent);
            }

            template <class WellState>
            RatioCheckTuple checkMaxWaterCutLimit(const WellEconProductionLimits& econ_production_limits,
                                                  const WellState& well_state,
                                                  const WellMapEntryType& map_entry) const
            {
                bool water_cut_limit_violated = false;
                int worst_offending_connection = INVALIDCONNECTION;
                bool last_connection = false;
                double violation_extent = -1.0;

                const int np = well_state.numPhases();
                const Opm::PhaseUsage& pu = phase_usage_;
                const int well_number = map_entry[0];

                assert(active_[Oil]);
                assert(active_[Water]);

                const double oil_rate = well_state.wellRates()[well_number * np + pu.phase_pos[ Oil ] ];
                const double water_rate = well_state.wellRates()[well_number * np + pu.phase_pos[ Water ] ];
                const double liquid_rate = oil_rate + water_rate;
                double water_cut;
                if (std::abs(liquid_rate) != 0.) {
                    water_cut = water_rate / liquid_rate;
                } else {
                    water_cut = 0.0;
                }

                const double max_water_cut_limit = econ_production_limits.maxWaterCut();
                if (water_cut > max_water_cut_limit) {
                    water_cut_limit_violated = true;
                }

                if (water_cut_limit_violated) {
                    // need to handle the worst_offending_connection
                    const int perf_start = map_entry[1];
                    const int perf_number = map_entry[2];

                    std::vector<double> water_cut_perf(perf_number);
                    for (int perf = 0; perf < perf_number; ++perf) {
                        const int i_perf = perf_start + perf;
                        const double oil_perf_rate = well_state.perfPhaseRates()[i_perf * np + pu.phase_pos[ Oil ] ];
                        const double water_perf_rate = well_state.perfPhaseRates()[i_perf * np + pu.phase_pos[ Water ] ];
                        const double liquid_perf_rate = oil_perf_rate + water_perf_rate;
                        if (std::abs(liquid_perf_rate) != 0.) {
                            water_cut_perf[perf] = water_perf_rate / liquid_perf_rate;
                        } else {
                            water_cut_perf[perf] = 0.;
                        }
                    }

                    last_connection = (perf_number == 1);
                    if (last_connection) {
                        worst_offending_connection = 0;
                        violation_extent = water_cut_perf[0] / max_water_cut_limit;
                        return std::make_tuple(water_cut_limit_violated, last_connection, worst_offending_connection, violation_extent);
                    }

                    double max_water_cut_perf = 0.;
                    for (int perf = 0; perf < perf_number; ++perf) {
                        if (water_cut_perf[perf] > max_water_cut_perf) {
                            worst_offending_connection = perf;
                            max_water_cut_perf = water_cut_perf[perf];
                        }
                    }

                    assert(max_water_cut_perf != 0.);
                    assert((worst_offending_connection >= 0) && (worst_offending_connection < perf_number));

                    violation_extent = max_water_cut_perf / max_water_cut_limit;
                }

                return std::make_tuple(water_cut_limit_violated, last_connection, worst_offending_connection, violation_extent);
            }





            template <class WellState>
            void updateWellStateWithTarget(const WellControls* wc,
                                           const int current,
                                           const int well_index,
                                           WellState& xw) const
            {
                // number of phases
                const int np = wells().number_of_phases;
                // Updating well state and primary variables.
                // Target values are used as initial conditions for BHP, THP, and SURFACE_RATE
                const double target = well_controls_iget_target(wc, current);
                const double* distr = well_controls_iget_distr(wc, current);
                switch (well_controls_iget_type(wc, current)) {
                case BHP:
                    xw.bhp()[well_index] = target;
                    break;

                case THP: {
                    double aqua = 0.0;
                    double liquid = 0.0;
                    double vapour = 0.0;

                    const Opm::PhaseUsage& pu = phase_usage_;

                    if (active_[ Water ]) {
                        aqua = xw.wellRates()[well_index*np + pu.phase_pos[ Water ] ];
                    }
                    if (active_[ Oil ]) {
                        liquid = xw.wellRates()[well_index*np + pu.phase_pos[ Oil ] ];
                    }
                    if (active_[ Gas ]) {
                        vapour = xw.wellRates()[well_index*np + pu.phase_pos[ Gas ] ];
                    }

                    const int vfp        = well_controls_iget_vfp(wc, current);
                    const double& thp    = well_controls_iget_target(wc, current);
                    const double& alq    = well_controls_iget_alq(wc, current);

                    //Set *BHP* target by calculating bhp from THP
                    const WellType& well_type = wells().type[well_index];

                    // pick the density in the top layer
                    const int perf = wells().well_connpos[well_index];
                    const double rho = well_perforation_densities_[perf];

                    if (well_type == INJECTOR) {
                        double dp = wellhelpers::computeHydrostaticCorrection(
                                    wells(), well_index, vfp_properties_->getInj()->getTable(vfp)->getDatumDepth(),
                                    rho, gravity_);

                        xw.bhp()[well_index] = vfp_properties_->getInj()->bhp(vfp, aqua, liquid, vapour, thp) - dp;
                    }
                    else if (well_type == PRODUCER) {
                        double dp = wellhelpers::computeHydrostaticCorrection(
                                    wells(), well_index, vfp_properties_->getProd()->getTable(vfp)->getDatumDepth(),
                                    rho, gravity_);

                        xw.bhp()[well_index] = vfp_properties_->getProd()->bhp(vfp, aqua, liquid, vapour, thp, alq) - dp;
                    }
                    else {
                        OPM_THROW(std::logic_error, "Expected PRODUCER or INJECTOR type of well");
                    }
                    break;
                }

                case RESERVOIR_RATE:
                    // No direct change to any observable quantity at
                    // surface condition.  In this case, use existing
                    // flow rates as initial conditions as reservoir
                    // rate acts only in aggregate.
                    break;

                case SURFACE_RATE:
                    // assign target value as initial guess for injectors and
                    // single phase producers (orat, grat, wrat)
                    const WellType& well_type = wells().type[well_index];
                    if (well_type == INJECTOR) {
                        for (int phase = 0; phase < np; ++phase) {
                            const double& compi = wells().comp_frac[np * well_index + phase];
                            // TODO: it was commented out from the master branch already.
                            //if (compi > 0.0) {
                            xw.wellRates()[np*well_index + phase] = target * compi;
                            //}
                        }
                    } else if (well_type == PRODUCER) {
                        // only set target as initial rates for single phase
                        // producers. (orat, grat and wrat, and not lrat)
                        // lrat will result in numPhasesWithTargetsUnderThisControl == 2
                        int numPhasesWithTargetsUnderThisControl = 0;
                        for (int phase = 0; phase < np; ++phase) {
                            if (distr[phase] > 0.0) {
                                numPhasesWithTargetsUnderThisControl += 1;
                            }
                        }
                        for (int phase = 0; phase < np; ++phase) {
                            if (distr[phase] > 0.0 && numPhasesWithTargetsUnderThisControl < 2 ) {
                                xw.wellRates()[np*well_index + phase] = target * distr[phase];
                            }
                        }
                    } else {
                        OPM_THROW(std::logic_error, "Expected PRODUCER or INJECTOR type of well");
                    }

                    break;
                } // end of switch


                std::vector<double> g = {1.0, 1.0, 0.01};
                if (well_controls_iget_type(wc, current) == RESERVOIR_RATE) {
                    for (int phase = 0; phase < np; ++phase) {
                        g[phase] = distr[phase];
                    }
                }

                // the number of wells
                const int nw = wells().number_of_wells;

                switch (well_controls_iget_type(wc, current)) {
                case THP:
                case BHP: {
                    const WellType& well_type = wells().type[well_index];
                    xw.wellSolutions()[nw*XvarWell + well_index] = 0.0;
                    if (well_type == INJECTOR) {
                        for (int p = 0; p < np; ++p) {
                            xw.wellSolutions()[nw*XvarWell + well_index] += xw.wellRates()[np*well_index + p] * wells().comp_frac[np*well_index + p];
                        }
                    } else {
                        for (int p = 0; p < np; ++p) {
                            xw.wellSolutions()[nw*XvarWell + well_index] += g[p] * xw.wellRates()[np*well_index + p];
                        }
                    }
                    break;
                }
                case RESERVOIR_RATE: // Intentional fall-through
                case SURFACE_RATE:
                    xw.wellSolutions()[nw*XvarWell + well_index] = xw.bhp()[well_index];
                    break;
                } // end of switch

                double tot_well_rate = 0.0;
                for (int p = 0; p < np; ++p)  {
                    tot_well_rate += g[p] * xw.wellRates()[np*well_index + p];
                }
                if(std::abs(tot_well_rate) > 0) {
                    if (active_[ Water ]) {
                        xw.wellSolutions()[WFrac*nw + well_index] = g[Water] * xw.wellRates()[np*well_index + Water] / tot_well_rate;
                    }
                    if (active_[ Gas ]) {
                        xw.wellSolutions()[GFrac*nw + well_index] = g[Gas] * xw.wellRates()[np*well_index + Gas] / tot_well_rate ;
                    }
                 } else {
                    if (active_[ Water ]) {
                        xw.wellSolutions()[WFrac*nw + well_index] =  wells().comp_frac[np*well_index + Water];
                    }

                    if (active_[ Gas ]) {
                        xw.wellSolutions()[GFrac*nw + well_index] =  wells().comp_frac[np*well_index + Gas];
                    }
                }

            }

        };


} // namespace Opm

#include "StandardWellsDense_impl.hpp"
#endif
