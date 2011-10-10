// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2009-2011 by Andreas Lauser                               *
 *   Copyright (C) 2009 by Karin Erbertseder                                 *
 *   Copyright (C) 2008 by Bernd Flemisch                                    *
 *   Institute for Modelling Hydraulic and Environmental Systems             *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 * \brief Quantities required by the single-phase, two-component box
 *        model defined on a vertex.
 */
#ifndef DUMUX_1P2C_VOLUME_VARIABLES_HH
#define DUMUX_1P2C_VOLUME_VARIABLES_HH

#include <dumux/boxmodels/common/boxvolumevariables.hh>
#include <dumux/material/fluidstates/compositionalfluidstate.hh>

#include "1p2cproperties.hh"

namespace Dumux
{

/*!
 * \ingroup OnePTwoCBoxModel
 * \ingroup BoxVolumeVariables
 * \brief Contains the quantities which are are constant within a
 *        finite volume in the single-phase, two-component model.
 */
template <class TypeTag>
class OnePTwoCVolumeVariables : public BoxVolumeVariables<TypeTag>
{
    typedef BoxVolumeVariables<TypeTag> ParentType;
    typedef typename GET_PROP_TYPE(TypeTag, VolumeVariables) Implementation;

    typedef typename GET_PROP_TYPE(TypeTag, Problem) Problem;
    typedef typename GET_PROP_TYPE(TypeTag, FVElementGeometry) FVElementGeometry;
    typedef typename GET_PROP_TYPE(TypeTag, PrimaryVariables) PrimaryVariables;
    typedef typename GET_PROP_TYPE(TypeTag, ElementContext) ElementContext;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;

    typedef typename GET_PROP_TYPE(TypeTag, OnePTwoCIndices) Indices;
    enum {
        pressureIdx = Indices::pressureIdx,
        x1Idx = Indices::x1Idx
    };

    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GridView::template Codim<0>::Entity Element;
    enum { dimWorld = GridView::dimensionworld };

    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef Dune::FieldVector<Scalar,dimWorld> Vector;

public:
    //! The type returned by the fluidState() method
    typedef Dumux::CompositionalFluidState<Scalar, FluidSystem> FluidState;

    /*!
     * \brief Update all quantities for a given control volume.
     *
     * \param priVars A vector containing the primary variables
     * \param problem The considered problem
     * \param element The considered element of the grid
     * \param elemGeom The finite-volume geometry in the box scheme
     * \param scvIdx  The index of the considered subcontrol volume
     * \param isOldSol Evaluate function with solution of current or previous time step
     */
    void update(const PrimaryVariables &priVars,
                const ElementContext &elemCtx,
                int scvIdx,
                int historyIdx)
    {
        ParentType::update(priVars, elemCtx, scvIdx, historyIdx);

        completeFluidState(fluidState_, elemCtx, scvIdx, historyIdx);
    
        const auto &spatialParams = elemCtx.problem().spatialParameters();
        porosity_ = spatialParams.porosity(elemCtx, scvIdx);
        tortuosity_ = spatialParams.tortuosity(elemCtx, scvIdx);
#warning "TODO: dispersivity"
        dispersivity_ = 0; // spatialParams.dispersivity(elemCtx, scvIdx);

        // Second instance of a parameter cache.
        // Could be avoided if diffusion coefficients also
        // became part of the fluid state.
        typename FluidSystem::ParameterCache paramCache;
        paramCache.updatePhase(fluidState_, /*phaseIdx=*/0);

        diffCoeff_ = FluidSystem::binaryDiffusionCoefficient(fluidState_,
                                                             paramCache,
                                                             /*phaseIdx=*/0,
                                                             /*comp0Idx=*/0,
                                                             /*comp1Idx=*/1);

        Valgrind::CheckDefined(porosity_);
        Valgrind::CheckDefined(tortuosity_);
        Valgrind::CheckDefined(dispersivity_);
        Valgrind::CheckDefined(diffCoeff_);

        // energy related quantities not contained in the fluid state
        asImp_().updateEnergy_(elemCtx, scvIdx, historyIdx);
    }

    /*!
     * \copydoc BoxModel::completeFluidState
     */
    static void completeFluidState(FluidState &fluidState,
                                   const ElementContext &elemCtx,
                                   int scvIdx,
                                   int historyIdx)
    {
        Implementation::updateTemperature_(fluidState, elemCtx, scvIdx, historyIdx);
        
        const auto &priVars = elemCtx.primaryVars(scvIdx, historyIdx);
        fluidState.setPressure(/*phaseIdx=*/0, priVars[pressureIdx]);

        Scalar x1 = priVars[x1Idx]; //mole or mass fraction of component 1
        if(!useMolarFormulation) //mass-fraction formulation
        {
            // convert mass to mole fractions
            Scalar M0 = FluidSystem::molarMass(/*compIdx=*/0);
            Scalar M1 = FluidSystem::molarMass(/*compIdx=*/1);
            //meanMolarMass if x1_ is a massfraction
            Scalar meanMolarMass = M0*M1/(M1 + x1*(M0 - M1));

            x1 *= meanMolarMass/M1;
        }
        fluidState.setMoleFraction(/*phaseIdx=*/0, /*compIdx=*/0, 1 - x1);
        fluidState.setMoleFraction(/*phaseIdx=*/0, /*compIdx=*/1, x1);

        typename FluidSystem::ParameterCache paramCache;
        paramCache.updatePhase(fluidState, /*phaseIdx=*/0);

        Scalar value;
        value = FluidSystem::density(fluidState, paramCache, /*phaseIdx=*/0);
        fluidState.setDensity(/*phaseIdx=*/0, value);
        value = FluidSystem::viscosity(fluidState, paramCache, /*phaseIdx=*/0);
        fluidState.setViscosity(/*phaseIdx=*/0, value);
    }

    /*!
     * \brief Return the fluid configuration at the given primary
     *        variables
     */
    const FluidState &fluidState() const
    { return fluidState_; }

    /*!
     * \brief Returns density the of the fluid phase.
     */
    Scalar density() const
    { return fluidState_.density(/*phaseIdx=*/0); }

    Scalar molarDensity() const
    { return fluidState_.molarDensity(/*phaseIdx=*/0);}

    /*!
     * \brief Returns mole fraction of a component in the phase
     *
     * \param compIdx The index of the component
     */
    Scalar moleFraction(int compIdx) const
    { return fluidState_.moleFraction(/*phaseIdx=*/0, compIdx); }

    /*!
     * \brief Returns mass fraction of a component in the phase
     * \param compIdx The index of the component
     */
    Scalar massFraction(int compIdx) const
    { return fluidState_.massFraction(/*phaseIdx=*/0, compIdx); }
    
    /*!
     * \brief Returns concentration of a component in the phase
     * \param compIdx The index of the component
     */
    Scalar molarity(int compIdx) const
    { return fluidState_.molarity(/*phaseIdx=*/0, compIdx); }

    /*!
     * \brief Returns the effective pressure of a given phase within
     *        the control volume.
     */
    Scalar pressure() const
    {  return fluidState_.pressure(/*phaseIdx=*/0);  }

    /*!
     * \brief Returns the binary diffusion coefficient in the fluid
     */
    Scalar diffCoeff() const
    { return diffCoeff_; }

    /*!
     * \brief Returns the tortuosity of the streamlines of the fluid.
     */
    Scalar tortuosity() const
    { return tortuosity_; }

    /*!
     * \brief Returns temperature inside the sub-control volume.
     *
     * Note that we assume thermodynamic equilibrium, i.e. the
     * temperature of the rock matrix and of all fluid phases are
     * identical.
     */
    Scalar temperature() const
    { return fluidState_.temperature(/*phaseIdx=*/0); }

    /*!
     * \brief Returns the dynamic viscosity \f$\mathrm{[Pa*s]}\f$ of a given phase
     *        within the control volume.
     */
    Scalar viscosity() const
    { return fluidState_.viscosity(/*phaseIdx=*/0); }

    /*!
     * \brief Returns the average porosity within the control volume.
     */
    Scalar porosity() const
    { return porosity_; }

protected:
    static void updateTemperature_(FluidState &fluidState,
                                   const ElementContext &elemCtx,
                                   int scvIdx,
                                   int historyIdx)
    {
        fluidState.setTemperature(elemCtx.problem().temperature(elemCtx, scvIdx));
    }
    
    template<class ParameterCache>
    static void updateEnthalpy_(FluidState &fluidState,
                                const ParameterCache &paramCache,
                                const ElementContext &elemCtx,
                                int scvIdx,
                                int historyIdx)
    { }

    /*!
     * \brief Called by update() to compute the energy related quantities
     */
    void updateEnergy_(const ElementContext &elemCtx,
                       int scvIdx,
                       int historyIdx)
    { }

    Scalar porosity_;    //!< Effective porosity within the control volume
    Scalar tortuosity_;
    Scalar dispersivity_;
    Scalar diffCoeff_;
    FluidState fluidState_;

private:
    Implementation &asImp_()
    { return *static_cast<Implementation*>(this); }

    const Implementation &asImp_() const
    { return *static_cast<const Implementation*>(this); }
};

}// end namepace

#endif
