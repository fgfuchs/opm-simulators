/*
  Copyright 2015 Statoil ASA.

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

#ifndef OPM_RELPERMDIAGNOSTICS_HEADER_INCLUDED
#define OPM_RELPERMDIAGNOSTICS_HEADER_INCLUDED

#include <vector>
#include <utility>

#include <opm/core/utility/linearInterpolation.hpp>
#include <opm/parser/eclipse/EclipseState/EclipseState.hpp>
#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/material/fluidmatrixinteractions/EclTwoPhaseMaterial.hpp>
#include <opm/material/fluidmatrixinteractions/EclEpsScalingPoints.hpp>

namespace Opm {
    
    enum Status {
        Pass,
        Error
    };

    enum FluidSystem {
        OilWater,
        OilGas,
        WaterGas,
        BlackOil
    };

    enum SaturationFunctionFamily {
        FamilyI,
        FamilyII,
        NoFamily
    };


    ///This class is intend to be a relpmer diganostics, to detect
    ///wrong input of relperm table and endpoints.
    class RelpermDiagnostics 
    {
    public:
        RelpermDiagnostics();
        RelpermDiagnostics(EclipseStateConstPtr eclstate);

        ///Display all the keywords.
        Status keywordsDisplay(EclipseStateConstPtr eclState);
        
        ///Check the phase that used.
        FluidSystem phaseCheck(EclipseStateConstPtr eclState, 
                               DeckConstPtr deck);

        
        ///Check saturation family I and II.
        Status satFamilyCheck(EclipseStateConstPtr eclState);
 
        ///Check saturation tables.
        Status tableCheck(EclipseStateConstPtr eclState, 
                          DeckConstPtr deck);

        ///Check endpoints in the saturation tables.
        Status endPointsCheck(DeckConstPtr deck,
                              EclipseStateConstPtr eclState);

    private:
        FluidSystem fluidsystem_;
        
        SaturationFunctionFamily satFamily_;
        std::vector<Opm::EclEpsScalingPointsInfo<double> > unscaledEpsInfo_;

        Status valueRangeCheck_(const std::vector<double>& value, 
                                bool isAscending);
        
        //Status endPointCheck_(const TableContainer&)

        ///For every table, need to deal with case by case.
        Status swofTableCheck_(const Opm::SwofTable& swofTables);
        Status sgofTableCheck_(const Opm::SgofTable& sgofTables);
        Status slgofTableCheck_(const Opm::SlgofTable& slgofTables);
        Status swfnTableCheck_(const Opm::SwfnTable& swfnTables);
        Status sgfnTableCheck_(const Opm::SgfnTable& sgfnTables);
        Status sof3TableCheck_(const Opm::Sof3Table& sof3Tables);
        Status sof2TableCheck_(const Opm::Sof2Table& sof2Tables);
        Status sgwfnTableCheck_(const Opm::SgwfnTable& sgwfnTables);
    };

    RelpermDiagnostics::RelpermDiagnostics()
    {}

    RelpermDiagnostics::RelpermDiagnostics(Opm::EclipseStateConstPtr eclState)
    {}


    Status RelpermDiagnostics::satFamilyCheck(Opm::EclipseStateConstPtr eclState)
    {
        const auto& tableManager = eclState->getTableManager();
        const TableContainer& swofTables = tableManager->getSwofTables();
        const TableContainer& slgofTables= tableManager->getSlgofTables();
        const TableContainer& sgofTables = tableManager->getSgofTables();
        const TableContainer& swfnTables = tableManager->getSwfnTables();
        const TableContainer& sgfnTables = tableManager->getSgfnTables();
        const TableContainer& sof3Tables = tableManager->getSof3Tables();
        const TableContainer& sof2Tables = tableManager->getSof2Tables();
        const TableContainer& sgwfnTables= tableManager->getSgwfnTables();

        bool family1 = (!sgofTables.empty() || !slgofTables.empty()) && !swofTables.empty();
        bool family2 = !swfnTables.empty() && !sgfnTables.empty() && (!sof3Tables.empty() || !sof2Tables.empty()) && !sgwfnTables.empty();

        if (family1 && family2) {
            throw std::invalid_argument("Saturation families should not be mixed \n"
                                        "Use either SGOF and SWOF or SGFN, SWFN and SOF3");
        }

        if (!family1 && !family2) {
            throw std::invalid_argument("Saturations function must be specified using either "
                                        "family 1 or family 2 keywords \n"
                                        "Use either SGOF and SWOF or SGFN, SWFN and SOF3" );
        }

        if (family1 && !family2) {
            satFamily_ = SaturationFunctionFamily::FamilyI;
            std::cout << "Using saturation Family I." << std::endl;
        } 
        if (!family1 && family2) {
            satFamily_ = SaturationFunctionFamily::FamilyII;
            std::cout << "Using saturation Family II." << std::endl;
        }

        return Opm::Status::Pass;
    }


    FluidSystem RelpermDiagnostics::phaseCheck(EclipseStateConstPtr eclState,
                                               DeckConstPtr deck)
    {
        bool hasWater = deck->hasKeyword("WATER");
        bool hasGas = deck->hasKeyword("GAS");
        bool hasOil = deck->hasKeyword("OIL");

        if (hasWater && hasGas && !hasOil) {
            std::cout << "This is Water-Gas system." << std::endl;
            return FluidSystem::WaterGas;
        }
        if (hasWater && hasOil && !hasGas) { 
            std::cout << "This is Oil-Water system." << std::endl;
            return FluidSystem::OilWater; 
        }
        if (hasOil && hasGas && !hasWater) { 
            std::cout << "This is Oil-Gas system." << std::endl;
            return FluidSystem::OilGas; 
        }
        if (hasOil && hasWater && hasGas) {
            std::cout << "This is Black-oil system." << std::endl;
            return FluidSystem::BlackOil;
        }
    } 

    Status RelpermDiagnostics::tableCheck(EclipseStateConstPtr eclState, 
                                          DeckConstPtr deck)
    {
        unsigned numSatRegions = static_cast<unsigned>(deck->getKeyword("TABDIMS")->getRecord(0)->getItem("NTSFUN")->getInt(0));
        const auto& tableManager = eclState->getTableManager();
        const TableContainer& swofTables = tableManager->getSwofTables();
        const TableContainer& slgofTables= tableManager->getSlgofTables();
        const TableContainer& sgofTables = tableManager->getSgofTables();
        const TableContainer& swfnTables = tableManager->getSwfnTables();
        const TableContainer& sgfnTables = tableManager->getSgfnTables();
        const TableContainer& sof3Tables = tableManager->getSof3Tables();
        const TableContainer& sof2Tables = tableManager->getSof2Tables();
        const TableContainer& sgwfnTables= tableManager->getSgwfnTables();

        for (unsigned satnumIdx = 0; satnumIdx < numSatRegions; ++satnumIdx) {
            if (deck->hasKeyword("SWOF")) {
                std::cout << "Starting check SWOF tables......" << std::endl;
                const auto& status = swofTableCheck_(swofTables.getTable<SwofTable>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
            if (deck->hasKeyword("SGOF")) {
                std::cout << "Starting check SGOF tables......" << std::endl;
                const auto& status = sgofTableCheck_(sgofTables.getTable<SgofTable>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
            if (deck->hasKeyword("SLGOF")) {
                std::cout << "Starting check SLGOF tables......" << std::endl;
                const auto& status = slgofTableCheck_(slgofTables.getTable<SlgofTable>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
            if (deck->hasKeyword("SWFN")) {
                std::cout << "Starting check SWFN tables......" << std::endl;
                const auto& status = swfnTableCheck_(swfnTables.getTable<SwfnTable>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
            if (deck->hasKeyword("SGFN")) {
                std::cout << "Starting check SGFN tables......" << std::endl;
                const auto& status = sgfnTableCheck_(sgfnTables.getTable<SgfnTable>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
            if (deck->hasKeyword("SOF3")) {
                std::cout << "Starting check SOF3 tables......" << std::endl;
                const auto& status = sof3TableCheck_(sof3Tables.getTable<Sof3Table>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
            if (deck->hasKeyword("SOF2")) {
                std::cout << "Starting check SOF2 tables......" << std::endl;
                const auto& status = sof2TableCheck_(sof2Tables.getTable<Sof2Table>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
            if (deck->hasKeyword("SGWFN")) {
                std::cout << "Starting check SOF2 tables......" << std::endl;
                const auto& status = sgwfnTableCheck_(sgwfnTables.getTable<SgwfnTable>(satnumIdx));
                if (status == Opm::Status::Pass) {
                    std::cout << "End of check, all values are reasonable." << std::endl;
                }
            }
        }
        return Opm::Status::Pass;
    }

    Status RelpermDiagnostics::swofTableCheck_(const Opm::SwofTable& swofTables)
    {
        const auto& sw = swofTables.getSwColumn();
        const auto& krw = swofTables.getKrwColumn();
        const auto& krow = swofTables.getKrowColumn();
        const auto& pc = swofTables.getPcowColumn();
        
        ///Check sw column.
        if (sw.front()< 0 || sw.back() > 1) {
            OPM_THROW(std::logic_error, "In SWOF table, saturation should be in range [0,1]");
        }
        ///TODO check endpoint sw.back() == 1. - Sor.
        ///Check krw column.
        if (krw.front() != 0) {
            OPM_THROW(std::logic_error, "In SWOF table, first value of krw should be 0");
        }
        if (krw.front() < 0 || krw.back() > 1) {
            OPM_THROW(std::logic_error, "In SWOF table, krw should be in range [0,1]");
        }

        ///Check krow column.
        if (krow.front() > 1 || krow.back() < 0) {
            OPM_THROW(std::logic_error, "In SWOF table, krow should be in range [0, 1]");
        }
        ///TODO check if run with gas.

        return Opm::Status::Pass;
    }


    Status RelpermDiagnostics::sgofTableCheck_(const Opm::SgofTable& sgofTables)
    {
        const auto& sg = sgofTables.getSgColumn();
        const auto& krg = sgofTables.getKrgColumn();
        const auto& krog = sgofTables.getKrogColumn();
                ///Check sw column.
        if (sg.front()< 0 || sg.back() > 1) {
            OPM_THROW(std::logic_error, "In SGOF table, saturation should be in range [0,1]");
        }
        if (sg.front() != 0) {
            OPM_THROW(std::logic_error, "In SGOF table, first value in sg column muse be 0");
        }
        ///TODO check endpoint sw.back() == 1. - Sor.
        ///Check krw column.
        if (krg.front() != 0) {
            OPM_THROW(std::logic_error, "In SGOF table, first value of krg should be 0");
        }
        if (krg.front() < 0 || krg.back() > 1) {
            OPM_THROW(std::logic_error, "In SGOF table, krg should be in range [0,1]");
        }

        ///Check krow column.
        if (krog.front() > 1 || krog.back() < 0) {
            OPM_THROW(std::logic_error, "In SGOF table, krog should be in range [0, 1]");
        }
        ///TODO check if run with water.

        return Opm::Status::Pass;
        
    }

    Status RelpermDiagnostics::slgofTableCheck_(const Opm::SlgofTable& slgofTables) 
    {
        const auto& sl = slgofTables.getSlColumn();
        const auto& krg = slgofTables.getKrgColumn();
        const auto& krog = slgofTables.getKrogColumn();

        ///Check sl column.
        ///TODO first value means sl = swco + sor
        if (sl.front()< 0 || sl.back() > 1) {
            OPM_THROW(std::logic_error, "In SLGOF table, saturation should be in range [0,1]");
        }
        if (sl.back() != 1) {
            OPM_THROW(std::logic_error, "In SLGOF table, last value in sl column muse be 1");
        }

        if (krg.front() > 1 || krg.back() < 0) {
            OPM_THROW(std::logic_error, "In SLGOF table, krg column shoule be in range [0, 1]");
        }
        if (krg.back() != 0) {
            OPM_THROW(std::logic_error, "In SLGOF table, last value in krg column should be 0");
        }

        if (krog.front() < 0 || krog.back() > 1) {
            OPM_THROW(std::logic_error, "In SLGOF table, krog column shoule be in range [0, 1]");

        }
        return Opm::Status::Pass;
    }

    Status RelpermDiagnostics::swfnTableCheck_(const Opm::SwfnTable& swfnTables)
    {
        const auto& sw = swfnTables.getSwColumn();
        const auto& krw = swfnTables.getKrwColumn();
        
        ///Check sw column.
        if (sw.front() < 0 || sw.back() > 1) {
            OPM_THROW(std::logic_error, "In SWFN table, saturation should be in range [0,1]");
        }
        
        ///Check krw column.
        if (krw.front() < 0 || krw.back() > 1) {
            OPM_THROW(std::logic_error, "In SWFN table, krw should be in range [0,1]");
        }
        if (krw.front() != 0) {
            OPM_THROW(std::logic_error, "In SWFN table, first value in krw column should be 0");
        }

        return Opm::Status::Pass;
    }

    
    Status RelpermDiagnostics::sgfnTableCheck_(const Opm::SgfnTable& sgfnTables)
    {
        const auto& sg = sgfnTables.getSgColumn();
        const auto& krg = sgfnTables.getKrgColumn();
        
        ///Check sg column.
        if (sg.front() < 0 || sg.back() > 1) {
            OPM_THROW(std::logic_error, "In SGFN table, saturation should be in range [0,1]");
        }
        
        ///Check krg column.
        if (krg.front() < 0 || krg.back() > 1) {
            OPM_THROW(std::logic_error, "In SGFN table, krg should be in range [0,1]");
        }
        if (krg.front() != 0) {
            OPM_THROW(std::logic_error, "In SGFN table, first value in krg column should be 0");
        }

        return Opm::Status::Pass;
    }

    Status RelpermDiagnostics::sof3TableCheck_(const Opm::Sof3Table& sof3Tables)
    {
        const auto& so = sof3Tables.getSoColumn();
        const auto& krow = sof3Tables.getKrowColumn();
        const auto& krog = sof3Tables.getKrogColumn();

        ///Check so column.
        ///TODO: The max so = 1 - Swco
        if (so.front() < 0 || so.back() > 1) {
            OPM_THROW(std::logic_error, "In SOF3 table, saturation should be in range [0,1]");
        }

        ///Check krow column.
        if (krow.front() < 0 || krow.back() > 1) {
            OPM_THROW(std::logic_error, "In SOF3 table, krow should be in range [0,1]");
        }
        if (krow.front() != 0) {
            OPM_THROW(std::logic_error, "In SOF3 table, first value in krow column should be 0");
        }

        ///Check krog column.
        if (krog.front() < 0 || krog.back() > 1) {
            OPM_THROW(std::logic_error, "In SOF3 table, krog should be in range [0,1]");
        }
        if (krog.front() != 0) {
            OPM_THROW(std::logic_error, "In SOF3 table, first value in krog column should be 0");
        }
    
        if (krog.back() != krow.back()) {
            OPM_THROW(std::logic_error, "In SOF3 table, max value in krog and krow should be the same");
        }
        
        return Opm::Status::Pass;
    }


    Status RelpermDiagnostics::sof2TableCheck_(const Opm::Sof2Table& sof2Tables)
    {
        const auto& so = sof2Tables.getSoColumn();
        const auto& kro = sof2Tables.getKroColumn();

        ///Check so column.
        ///TODO: The max so = 1 - Swco
        if (so.front() < 0 || so.back() > 1) {
            OPM_THROW(std::logic_error, "In SOF2 table, saturation should be in range [0,1]");
        }

        ///Check krow column.
        if (kro.front() < 0 || kro.back() > 1) {
            OPM_THROW(std::logic_error, "In SOF2 table, krow should be in range [0,1]");
        }
        if (kro.front() != 0) {
            OPM_THROW(std::logic_error, "In SOF2 table, first value in krow column should be 0");
        }

        return Opm::Status::Pass;
    }


    Status RelpermDiagnostics::sgwfnTableCheck_(const Opm::SgwfnTable& sgwfnTables)
    {
        const auto& sg = sgwfnTables.getSgColumn();
        const auto& krg = sgwfnTables.getKrgColumn();
        const auto& krgw = sgwfnTables.getKrgwColumn();
        
        ///Check sg column.
        if (sg.front() < 0 || sg.back() > 1) {
            OPM_THROW(std::logic_error, "In SGWFN table, saturation should be in range [0,1]");
        }

        ///Check krg column.
        if (krg.front() < 0 || krg.back() > 1) {
            OPM_THROW(std::logic_error, "In SGWFN table, krg column should be in range [0,1]");
        }
        if (krg.front() != 0) {
            OPM_THROW(std::logic_error, "In SGWFN table, first value in krg column should be 0");
        }

        ///Check krgw column.
        ///TODO check saturation sw = 1. - sg
        if (krgw.front() > 1 || krgw.back() < 0) {
            OPM_THROW(std::logic_error, "In SGWFN table, krgw column should be in range [0,1]");
        }
        if (krgw.back() != 0) {
            OPM_THROW(std::logic_error, "In SGWFN table, last value in krgw column should be 0");
        }

        return Opm::Status::Pass;
    }



    Status RelpermDiagnostics::valueRangeCheck_(const std::vector<double>& values,
                                                bool isAscending)
    {
        if (isAscending) {
            if (values.front() >1 || values.front() <0) {
                OPM_THROW(std::logic_error, "Values should be in range [0,1]");
            }
        } else {
            if (values.front() <0 || values.front() >1) {
                OPM_THROW(std::logic_error, "Values should be in range [0,1]");
            }
        }
        

        return Opm::Status::Pass;
    }


    Status RelpermDiagnostics::endPointsCheck(DeckConstPtr deck,
                                              EclipseStateConstPtr eclState)
    {
        // get the number of saturation regions and the number of cells in the deck

        unsigned numSatRegions = static_cast<unsigned>(deck->getKeyword("TABDIMS")->getRecord(0)->getItem("NTSFUN")->getInt(0));
        unscaledEpsInfo_.resize(numSatRegions);

        bool hasWater = deck->hasKeyword("WATER");
        bool hasGas = deck->hasKeyword("GAS");
        bool hasOil = deck->hasKeyword("OIL");

        auto tables = eclState->getTableManager();
        const TableContainer&  swofTables = tables->getSwofTables();
        const TableContainer&  sgofTables = tables->getSgofTables();
        const TableContainer& slgofTables = tables->getSlgofTables();
        const TableContainer&  sof3Tables = tables->getSof3Tables();


        for (unsigned satnumIdx = 0; satnumIdx < numSatRegions; ++satnumIdx) {
             unscaledEpsInfo_[satnumIdx].extractUnscaled(deck, eclState, satnumIdx);
             unscaledEpsInfo_[satnumIdx].print();
             ///Consistency check.
             std::cout << "End-Points consistency check......"  << std::endl;
             assert(unscaledEpsInfo_[satnumIdx].Sgu < (1. - unscaledEpsInfo_[satnumIdx].Swl));
             assert(unscaledEpsInfo_[satnumIdx].Sgl < (1. - unscaledEpsInfo_[satnumIdx].Swu));


             ///Krow(Sou) == Krog(Sou) for three-phase
             /// means Krow(Swco) == Krog(Sgco)
             double krow_value = 1e20;
             double krog_value = 1e-20;
             if (hasWater && hasGas && hasOil) {
                 if (satFamily_ == SaturationFunctionFamily::FamilyI) {
                     if (!sgofTables.empty()) {
                         auto sg = sgofTables.getTable<SgofTable>(satnumIdx).getSgColumn();
                         auto krog = sgofTables.getTable<SgofTable>(satnumIdx).getKrogColumn();
                         krog_value=Opm::linearInterpolation(sg, krog,unscaledEpsInfo_[satnumIdx].Sgl);
                     } else {
                         assert(!slgofTables.empty());
                         auto sl = slgofTables.getTable<SlgofTable>(satnumIdx).getSlColumn();
                         auto krog = slgofTables.getTable<SlgofTable>(satnumIdx).getKrogColumn();
                         krog_value=Opm::linearInterpolation(sl, krog, unscaledEpsInfo_[satnumIdx].Sgl);                        
                     }
                 
                 auto sw = swofTables.getTable<SwofTable>(satnumIdx).getSwColumn();
                 auto krow = swofTables.getTable<SwofTable>(satnumIdx).getKrowColumn();
                 krow_value = Opm::linearInterpolation(sw, krow,unscaledEpsInfo_[satnumIdx].Swl);
                 }
                 if (satFamily_ == SaturationFunctionFamily::FamilyII) {
                     assert(!sof3Table.empty());
                     const double Sou = 1.- unscaledEpsInfo_[satnumIdx].Swl - unscaledEpsInfo_[satnumIdx].Sgl;
                     auto so = sof3Tables.getTable<Sof3Table>(satnumIdx).getSoColumn();
                     auto krow = sof3Tables.getTable<Sof3Table>(satnumIdx).getKrowColumn();
                     auto krog = sof3Tables.getTable<Sof3Table>(satnumIdx).getKrogColumn();
                     krow_value = Opm::linearInterpolation(so, krow, Sou);
                     krog_value = Opm::linearInterpolation(so, krog, Sou);
                 }
                 assert(krow_value == krog_value);
             }
             ///Krw(Sw=0)=Krg(Sg=0)=Krow(So=0)=Krog(So=0)=0.
             ///Mobile fluid requirements
             assert(((unscaledEpsInfo_[satnumIdx].Sowcr + unscaledEpsInfo_.[satnumIdx].Swcr)-1) < 1e-30);
             assert(((unscaledEpsInfo_[satnumIdx].Sogcr + unscaledEpsInfo_.[satnumIdx].Sgcr + unscaledEpsInfo_[satnumIdx].Swl) - 1 ) < 1e-30);
        }
        std::cout << "End of Check. All values are resonable." << std::endl;
        return Opm::Status::Pass;
    }



} //namespace Opm

#endif // OPM_RELPERMDIAGNOSTICS_HEADER_INCLUDED
