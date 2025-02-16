// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/
/*!
 * \file
 *
 * \brief Contains the classes required to extend the black-oil model by energy.
 */
#ifndef EWOMS_BLACK_OIL_ENERGY_MODULE_HH
#define EWOMS_BLACK_OIL_ENERGY_MODULE_HH

#include "blackoilproperties.hh"
#include <opm/models/io/vtkblackoilenergymodule.hh>
#include <opm/models/common/quantitycallbacks.hh>

#include <opm/material/common/Tabulated1DFunction.hpp>

#include <opm/material/common/Valgrind.hpp>

#include <dune/common/fvector.hh>

#include <string>

namespace Opm {
/*!
 * \ingroup BlackOil
 * \brief Contains the high level supplements required to extend the black oil
 *        model by energy.
 */
template <class TypeTag, bool enableEnergyV = getPropValue<TypeTag, Properties::EnableEnergy>()>
class BlackOilEnergyModule
{
    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using PrimaryVariables = GetPropType<TypeTag, Properties::PrimaryVariables>;
    using IntensiveQuantities = GetPropType<TypeTag, Properties::IntensiveQuantities>;
    using ExtensiveQuantities = GetPropType<TypeTag, Properties::ExtensiveQuantities>;
    using Model = GetPropType<TypeTag, Properties::Model>;
    using Simulator = GetPropType<TypeTag, Properties::Simulator>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using EqVector = GetPropType<TypeTag, Properties::EqVector>;
    using RateVector = GetPropType<TypeTag, Properties::RateVector>;
    using Indices = GetPropType<TypeTag, Properties::Indices>;

    static constexpr unsigned temperatureIdx = Indices::temperatureIdx;
    static constexpr unsigned contiEnergyEqIdx = Indices::contiEnergyEqIdx;

    static constexpr unsigned enableEnergy = enableEnergyV;
    static constexpr unsigned numEq = getPropValue<TypeTag, Properties::NumEq>();
    static constexpr unsigned numPhases = FluidSystem::numPhases;

public:
    /*!
     * \brief Register all run-time parameters for the black-oil energy module.
     */
    static void registerParameters()
    {
        if constexpr (enableEnergy)
            VtkBlackOilEnergyModule<TypeTag>::registerParameters();
    }

    /*!
     * \brief Register all energy specific VTK and ECL output modules.
     */
    static void registerOutputModules(Model& model,
                                      Simulator& simulator)
    {
        if constexpr (enableEnergy)
            model.addOutputModule(new VtkBlackOilEnergyModule<TypeTag>(simulator));
    }

    static bool primaryVarApplies(unsigned pvIdx)
    {
        if constexpr (enableEnergy)
            return pvIdx == temperatureIdx;
        else
            return false;

    }

    static std::string primaryVarName([[maybe_unused]] unsigned pvIdx)
    {
        assert(primaryVarApplies(pvIdx));

        return "temperature";
    }

    static Scalar primaryVarWeight([[maybe_unused]] unsigned pvIdx)
    {
        assert(primaryVarApplies(pvIdx));

        // TODO: it may be beneficial to chose this differently.
        return static_cast<Scalar>(1.0);
    }

    static bool eqApplies(unsigned eqIdx)
    {
        if constexpr (enableEnergy)
            return eqIdx == contiEnergyEqIdx;
        else
            return false;
    }

    static std::string eqName([[maybe_unused]] unsigned eqIdx)
    {
        assert(eqApplies(eqIdx));

        return "conti^energy";
    }

    static Scalar eqWeight([[maybe_unused]] unsigned eqIdx)
    {
        assert(eqApplies(eqIdx));

        return 1.0;
    }

    // must be called after water storage is computed
    template <class LhsEval>
    static void addStorage(Dune::FieldVector<LhsEval, numEq>& storage,
                           const IntensiveQuantities& intQuants)
    {
        if constexpr (enableEnergy) {
            const auto& poro = decay<LhsEval>(intQuants.porosity());

            // accumulate the internal energy of the fluids
            const auto& fs = intQuants.fluidState();
            for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++ phaseIdx) {
                if (!FluidSystem::phaseIsActive(phaseIdx))
                    continue;

                const auto& u = decay<LhsEval>(fs.internalEnergy(phaseIdx));
                const auto& S = decay<LhsEval>(fs.saturation(phaseIdx));
                const auto& rho = decay<LhsEval>(fs.density(phaseIdx));

                storage[contiEnergyEqIdx] += poro*S*u*rho;
            }

            // add the internal energy of the rock
            Scalar refPoro = intQuants.referencePorosity();
            const auto& uRock = decay<LhsEval>(intQuants.rockInternalEnergy());
            storage[contiEnergyEqIdx] += (1.0 - refPoro)*uRock;
            storage[contiEnergyEqIdx] *= getPropValue<TypeTag, Properties::BlackOilEnergyScalingFactor>();
        }
    }

    static void computeFlux([[maybe_unused]] RateVector& flux,
                            [[maybe_unused]] const ElementContext& elemCtx,
                            [[maybe_unused]] unsigned scvfIdx,
                            [[maybe_unused]] unsigned timeIdx)
    {
        if constexpr (enableEnergy) {
            flux[contiEnergyEqIdx] = 0.0;

            const auto& extQuants = elemCtx.extensiveQuantities(scvfIdx, timeIdx);
            unsigned focusIdx = elemCtx.focusDofIndex();
            for (unsigned phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx) {
                if (!FluidSystem::phaseIsActive(phaseIdx))
                    continue;

                unsigned upIdx = extQuants.upstreamIndex(phaseIdx);
                if (upIdx == focusIdx)
                    addPhaseEnthalpyFlux_<Evaluation>(flux, phaseIdx, elemCtx, scvfIdx, timeIdx);
                else
                    addPhaseEnthalpyFlux_<Scalar>(flux, phaseIdx, elemCtx, scvfIdx, timeIdx);
            }

            // diffusive energy flux
            flux[contiEnergyEqIdx] += extQuants.energyFlux();
            flux[contiEnergyEqIdx] *= getPropValue<TypeTag, Properties::BlackOilEnergyScalingFactor>();
        }
    }

    template <class UpstreamEval>
    static void addPhaseEnthalpyFlux_(RateVector& flux,
                                      unsigned phaseIdx,
                                      const ElementContext& elemCtx,
                                      unsigned scvfIdx,
                                      unsigned timeIdx)
    {
        const auto& extQuants = elemCtx.extensiveQuantities(scvfIdx, timeIdx);
        unsigned upIdx = extQuants.upstreamIndex(phaseIdx);
        const auto& up = elemCtx.intensiveQuantities(upIdx, timeIdx);
        const auto& fs = up.fluidState();

        const auto& volFlux = extQuants.volumeFlux(phaseIdx);
        flux[contiEnergyEqIdx] +=
            decay<UpstreamEval>(fs.enthalpy(phaseIdx))
            * decay<UpstreamEval>(fs.density(phaseIdx))
            * volFlux;
    }

    static void addToEnthalpyRate(RateVector& flux,
                                  const Evaluation& hRate)
    {
        if constexpr (enableEnergy)
            flux[contiEnergyEqIdx] += hRate;
    }

    /*!
     * \brief Assign the energy specific primary variables to a PrimaryVariables object
     */
    static void assignPrimaryVars(PrimaryVariables& priVars,
                                  Scalar)
    {
        if constexpr (enableEnergy)
            priVars[temperatureIdx] = temperatureIdx;
    }

    /*!
     * \brief Assign the energy specific primary variables to a PrimaryVariables object
     */
    template <class FluidState>
    static void assignPrimaryVars(PrimaryVariables& priVars,
                                  const FluidState& fluidState)
    {
        if constexpr (enableEnergy)
            priVars[temperatureIdx] = fluidState.temperature(/*phaseIdx=*/0);
    }

    /*!
     * \brief Do a Newton-Raphson update the primary variables of the energys.
     */
    static void updatePrimaryVars(PrimaryVariables& newPv,
                                  const PrimaryVariables& oldPv,
                                  const EqVector& delta)
    {
        if constexpr (enableEnergy)
            // do a plain unchopped Newton update
            newPv[temperatureIdx] = oldPv[temperatureIdx] - delta[temperatureIdx];
    }

    /*!
     * \brief Return how much a Newton-Raphson update is considered an error
     */
    static Scalar computeUpdateError(const PrimaryVariables&,
                                     const EqVector&)
    {
        // do not consider consider the cange of energy primary variables for
        // convergence
        // TODO: maybe this should be changed
        return static_cast<Scalar>(0.0);
    }

    /*!
     * \brief Return how much a residual is considered an error
     */
    static Scalar computeResidualError(const EqVector& resid)
    {
        // do not weight the residual of energy when it comes to convergence
        return std::abs(scalarValue(resid[contiEnergyEqIdx]));
    }

    template <class DofEntity>
    static void serializeEntity(const Model& model, std::ostream& outstream, const DofEntity& dof)
    {
        if constexpr (enableEnergy) {
            unsigned dofIdx = model.dofMapper().index(dof);
            const PrimaryVariables& priVars = model.solution(/*timeIdx=*/0)[dofIdx];
            outstream << priVars[temperatureIdx];
        }
    }

    template <class DofEntity>
    static void deserializeEntity(Model& model, std::istream& instream, const DofEntity& dof)
    {
        if constexpr (enableEnergy) {
            unsigned dofIdx = model.dofMapper().index(dof);
            PrimaryVariables& priVars0 = model.solution(/*timeIdx=*/0)[dofIdx];
            PrimaryVariables& priVars1 = model.solution(/*timeIdx=*/1)[dofIdx];

            instream >> priVars0[temperatureIdx];

            // set the primary variables for the beginning of the current time step.
            priVars1 = priVars0[temperatureIdx];
        }
    }
};

/*!
 * \ingroup BlackOil
 * \class Opm::BlackOilEnergyIntensiveQuantities
 *
 * \brief Provides the volumetric quantities required for the equations needed by the
 *        energys extension of the black-oil model.
 */
template <class TypeTag, bool enableEnergyV = getPropValue<TypeTag, Properties::EnableEnergy>()>
class BlackOilEnergyIntensiveQuantities
{
    using Implementation = GetPropType<TypeTag, Properties::IntensiveQuantities>;

    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using PrimaryVariables = GetPropType<TypeTag, Properties::PrimaryVariables>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using SolidEnergyLaw = GetPropType<TypeTag, Properties::SolidEnergyLaw>;
    using ThermalConductionLaw = GetPropType<TypeTag, Properties::ThermalConductionLaw>;
    using Indices = GetPropType<TypeTag, Properties::Indices>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;

    using EnergyModule = BlackOilEnergyModule<TypeTag>;

    enum { numPhases = getPropValue<TypeTag, Properties::NumPhases>() };
    static constexpr int temperatureIdx = Indices::temperatureIdx;
    static constexpr int waterPhaseIdx = FluidSystem::waterPhaseIdx;


public:
    /*!
     * \brief Update the temperature of the intensive quantity's fluid state
     *
     */
    void updateTemperature_(const ElementContext& elemCtx,
                            unsigned dofIdx,
                            unsigned timeIdx)
    {
        auto& fs = asImp_().fluidState_;
        const auto& priVars = elemCtx.primaryVars(dofIdx, timeIdx);

        // set temperature
        fs.setTemperature(priVars.makeEvaluation(temperatureIdx, timeIdx, elemCtx.linearizationType()));
    }

    /*!
     * \brief Compute the intensive quantities needed to handle energy conservation
     *
     */
    void updateEnergyQuantities_(const ElementContext& elemCtx,
                                 unsigned dofIdx,
                                 unsigned timeIdx,
                                 const typename FluidSystem::template ParameterCache<Evaluation>& paramCache)
    {
        auto& fs = asImp_().fluidState_;

        // compute the specific enthalpy of the fluids, the specific enthalpy of the rock
        // and the thermal condictivity coefficients
        for (int phaseIdx = 0; phaseIdx < numPhases; ++ phaseIdx) {
            if (!FluidSystem::phaseIsActive(phaseIdx)) {
                continue;
            }

            const auto& h = FluidSystem::enthalpy(fs, paramCache, phaseIdx);
            fs.setEnthalpy(phaseIdx, h);
        }

        const auto& solidEnergyLawParams = elemCtx.problem().solidEnergyLawParams(elemCtx, dofIdx, timeIdx);
        rockInternalEnergy_ = SolidEnergyLaw::solidInternalEnergy(solidEnergyLawParams, fs);

        const auto& thermalConductionLawParams = elemCtx.problem().thermalConductionLawParams(elemCtx, dofIdx, timeIdx);
        totalThermalConductivity_ = ThermalConductionLaw::thermalConductivity(thermalConductionLawParams, fs);
    }

    const Evaluation& rockInternalEnergy() const
    { return rockInternalEnergy_; }

    const Evaluation& totalThermalConductivity() const
    { return totalThermalConductivity_; }

protected:
    Implementation& asImp_()
    { return *static_cast<Implementation*>(this); }

    Evaluation rockInternalEnergy_;
    Evaluation totalThermalConductivity_;
};

template <class TypeTag>
class BlackOilEnergyIntensiveQuantities<TypeTag, false>
{
    using Implementation = GetPropType<TypeTag, Properties::IntensiveQuantities>;

    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using Scalar = GetPropType<TypeTag, Properties::Scalar>;

    static constexpr bool enableTemperature = getPropValue<TypeTag, Properties::EnableTemperature>();

public:
    void updateTemperature_([[maybe_unused]] const ElementContext& elemCtx,
                            [[maybe_unused]] unsigned dofIdx,
                            [[maybe_unused]] unsigned timeIdx)
    {
        if constexpr (enableTemperature) {
            // even if energy is conserved, the temperature can vary over the spatial
            // domain if the EnableTemperature property is set to true
            auto& fs = asImp_().fluidState_;
            Scalar T = elemCtx.problem().temperature(elemCtx, dofIdx, timeIdx);
            fs.setTemperature(T);
        }
    }

    void updateEnergyQuantities_(const ElementContext&,
                                 unsigned,
                                 unsigned,
                                 const typename FluidSystem::template ParameterCache<Evaluation>&)
    { }

    const Evaluation& rockInternalEnergy() const
    { throw std::logic_error("Requested the rock internal energy, which is "
                             "unavailable because energy is not conserved"); }

    const Evaluation& totalThermalConductivity() const
    { throw std::logic_error("Requested the total thermal conductivity, which is "
                             "unavailable because energy is not conserved"); }

protected:
    Implementation& asImp_()
    { return *static_cast<Implementation*>(this); }
};


/*!
 * \ingroup BlackOil
 * \class Opm::BlackOilEnergyExtensiveQuantities
 *
 * \brief Provides the energy specific extensive quantities to the generic black-oil
 *        module's extensive quantities.
 */
template <class TypeTag, bool enableEnergyV = getPropValue<TypeTag, Properties::EnableEnergy>()>
class BlackOilEnergyExtensiveQuantities
{
    using Implementation = GetPropType<TypeTag, Properties::ExtensiveQuantities>;

    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using IntensiveQuantities = GetPropType<TypeTag, Properties::IntensiveQuantities>;
    using ExtensiveQuantities = GetPropType<TypeTag, Properties::ExtensiveQuantities>;
    using FluidSystem = GetPropType<TypeTag, Properties::FluidSystem>;
    using GridView = GetPropType<TypeTag, Properties::GridView>;

    using Toolbox = MathToolbox<Evaluation>;

    using EnergyModule = BlackOilEnergyModule<TypeTag>;

    static const int dimWorld = GridView::dimensionworld;
    using DimVector = Dune::FieldVector<Scalar, dimWorld>;
    using DimEvalVector = Dune::FieldVector<Evaluation, dimWorld>;

public:
    void updateEnergy(const ElementContext& elemCtx,
                      unsigned scvfIdx,
                      unsigned timeIdx)
    {
        const auto& stencil = elemCtx.stencil(timeIdx);
        const auto& scvf = stencil.interiorFace(scvfIdx);

        Scalar faceArea = scvf.area();
        unsigned inIdx = scvf.interiorIndex();
        unsigned exIdx = scvf.exteriorIndex();
        const auto& inIq = elemCtx.intensiveQuantities(inIdx, timeIdx);
        const auto& exIq = elemCtx.intensiveQuantities(exIdx, timeIdx);
        const auto& inFs = inIq.fluidState();
        const auto& exFs = exIq.fluidState();

        Evaluation deltaT;
        if (elemCtx.focusDofIndex() == inIdx)
            deltaT =
                decay<Scalar>(exFs.temperature(/*phaseIdx=*/0))
                - inFs.temperature(/*phaseIdx=*/0);
        else if (elemCtx.focusDofIndex() == exIdx)
            deltaT =
                exFs.temperature(/*phaseIdx=*/0)
                - decay<Scalar>(inFs.temperature(/*phaseIdx=*/0));
        else
            deltaT =
                decay<Scalar>(exFs.temperature(/*phaseIdx=*/0))
                - decay<Scalar>(inFs.temperature(/*phaseIdx=*/0));

        Evaluation inLambda;
        if (elemCtx.focusDofIndex() == inIdx)
            inLambda = inIq.totalThermalConductivity();
        else
            inLambda = decay<Scalar>(inIq.totalThermalConductivity());

        Evaluation exLambda;
        if (elemCtx.focusDofIndex() == exIdx)
            exLambda = exIq.totalThermalConductivity();
        else
            exLambda = decay<Scalar>(exIq.totalThermalConductivity());

        auto distVec = elemCtx.pos(exIdx, timeIdx);
        distVec -= elemCtx.pos(inIdx, timeIdx);

        Evaluation H;
        if (inLambda > 0.0 && exLambda > 0.0) {
            // compute the "thermal transmissibility". In contrast to the normal
            // transmissibility this cannot be done as a preprocessing step because the
            // average thermal thermal conductivity is analogous to the permeability but
            // depends on the solution.
            Scalar inAlpha = elemCtx.problem().thermalHalfTransmissibilityIn(elemCtx, scvfIdx, timeIdx);
            Scalar outAlpha = elemCtx.problem().thermalHalfTransmissibilityOut(elemCtx, scvfIdx, timeIdx);
            const Evaluation& inH = inLambda*inAlpha;
            const Evaluation& exH = exLambda*outAlpha;
            H = 1.0/(1.0/inH + 1.0/exH);
        }
        else
            H = 0.0;

        energyFlux_ = deltaT * (-H/faceArea);
    }

    template <class Context, class BoundaryFluidState>
    void updateEnergyBoundary(const Context& ctx,
                              unsigned scvfIdx,
                              unsigned timeIdx,
                              const BoundaryFluidState& boundaryFs)
    {
        const auto& stencil = ctx.stencil(timeIdx);
        const auto& scvf = stencil.boundaryFace(scvfIdx);

        unsigned inIdx = scvf.interiorIndex();
        const auto& inIq = ctx.intensiveQuantities(inIdx, timeIdx);
        const auto& inFs = inIq.fluidState();

        Evaluation deltaT;
        if (ctx.focusDofIndex() == inIdx)
            deltaT =
                boundaryFs.temperature(/*phaseIdx=*/0)
                - inFs.temperature(/*phaseIdx=*/0);
        else
            deltaT =
                decay<Scalar>(boundaryFs.temperature(/*phaseIdx=*/0))
                - decay<Scalar>(inFs.temperature(/*phaseIdx=*/0));

        Evaluation lambda;
        if (ctx.focusDofIndex() == inIdx)
            lambda = inIq.totalThermalConductivity();
        else
            lambda = decay<Scalar>(inIq.totalThermalConductivity());

        auto distVec = scvf.integrationPos();
        distVec -= ctx.pos(inIdx, timeIdx);

        if (lambda > 0.0) {
            // compute the "thermal transmissibility". In contrast to the normal
            // transmissibility this cannot be done as a preprocessing step because the
            // average thermal conductivity is analogous to the permeability but depends
            // on the solution.
            Scalar alpha = ctx.problem().thermalHalfTransmissibilityBoundary(ctx, scvfIdx);
            energyFlux_ = deltaT*lambda*(-alpha);
        }
        else
            energyFlux_ = 0.0;
    }

    const Evaluation& energyFlux()  const
    { return energyFlux_; }

private:
    Implementation& asImp_()
    { return *static_cast<Implementation*>(this); }

    Evaluation energyFlux_;
};

template <class TypeTag>
class BlackOilEnergyExtensiveQuantities<TypeTag, false>
{
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;

public:
    void updateEnergy(const ElementContext&,
                      unsigned,
                      unsigned)
    {}

    template <class Context, class BoundaryFluidState>
    void updateEnergyBoundary(const Context&,
                              unsigned,
                              unsigned,
                              const BoundaryFluidState&)
    {}

    const Evaluation& energyFlux()  const
    { throw std::logic_error("Requested the energy flux, but energy is not conserved"); }
};


} // namespace Opm

#endif
