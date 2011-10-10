// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2008-2009 by Klaus Mosthaf                                *
 *   Copyright (C) 2008-2009 by Andreas Lauser                               *
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
 *
 * \brief Definition of the spatial parameters for the 1p2c
 *        tissue-tumor problem
 */
#ifndef DUMUX_TISSUE_TUMOR_SPATIAL_PARAMETERS_HH
#define DUMUX_TISSUE_TUMOR_SPATIAL_PARAMETERS_HH

#include <dumux/material/spatialparameters/boxspatialparameters1p.hh>
#include <dumux/material/fluidmatrixinteractions/2p/linearmaterial.hh>
#include <dumux/material/fluidmatrixinteractions/2p/regularizedbrookscorey.hh>
#include <dumux/material/fluidmatrixinteractions/2p/efftoabslaw.hh>

namespace Dumux
{

/*!
 * \ingroup OnePTwoCBoxModel
 * \ingroup BoxTestProblems
 *
 * \brief Definition of the spatial parameters for the 1p2c
 *        tissue-tumor problem
 */
template<class TypeTag>
class TissueTumorSpatialParameters : public BoxSpatialParametersOneP<TypeTag>
{
    typedef BoxSpatialParametersOneP<TypeTag> ParentType;
    typedef typename GET_PROP_TYPE(TypeTag, Grid) Grid;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename Grid::ctype CoordScalar;
    enum {
        dim=GridView::dimension,
        dimWorld=GridView::dimensionworld
    };

    typedef Dune::FieldVector<CoordScalar,dim> LocalPosition;
    typedef Dune::FieldVector<CoordScalar,dimWorld> GlobalPosition;
    typedef Dune::FieldVector<CoordScalar,dimWorld> Vector;


    typedef typename GET_PROP_TYPE(TypeTag, SolutionVector) SolutionVector;

    typedef typename GET_PROP_TYPE(TypeTag, VolumeVariables) VolumeVariables;
    typedef typename GET_PROP_TYPE(TypeTag, FluxVariables) FluxVariables;
    typedef typename GET_PROP_TYPE(TypeTag, ElementContext) ElementContext;

    typedef typename GET_PROP_TYPE(TypeTag, FVElementGeometry) FVElementGeometry;
    typedef typename GridView::template Codim<0>::Entity Element;

    typedef RegularizedBrooksCorey<Scalar> EffMaterialLaw;
    //typedef LinearMaterial<Scalar> EffMaterialLaw;
public:
    TissueTumorSpatialParameters(const GridView &gv)
        : ParentType(gv)
    {
        permTumor_ = 2.142e-11;
        permTissue_ = 4.424e-12;
        porosityTumor_ = 0.31;
        porosityTissue_ = 0.13;
        tortuosityTumor_ = 0.706;
        tortuosityTissue_ = 0.280;
    }

    ~TissueTumorSpatialParameters()
    {}


    /*!
     * \brief Define the intrinsic permeability \f$\mathrm{[m^2]}\f$.
     *
     * \param element The current finite element
     * \param fvElemGeom The current finite volume geometry of the element
     * \param scvIdx The index of the sub-control volume
     */
    template <class Context>
    Scalar intrinsicPermeability(const Context &context, int localIdx) const
    {
        const GlobalPosition &pos = context.pos(localIdx);
        if (isTumor_(pos))
            return permTumor_;
        else
            return permTissue_;
    }

    /*!
     * \brief Define the porosity \f$\mathrm{[-]}\f$.
     *
     * \param element The finite element
     * \param fvElemGeom The finite volume geometry
     * \param scvIdx The local index of the sub-control volume where
     */
    template <class Context>
    Scalar porosity(const Context &context, int localIdx) const
    {
        const GlobalPosition &pos = context.pos(localIdx);
        if (isTumor_(pos))
            return porosityTumor_;
        else
            return porosityTissue_;
    }

    /*!
     * \brief Define the tortuosity \f$\mathrm{[-]}\f$.
     *
     * \param element The finite element
     * \param fvElemGeom The finite volume geometry
     * \param scvIdx The local index of the sub-control volume where
     */
    template <class Context>
    Scalar tortuosity(const Context &context, int localIdx) const
    {
        const GlobalPosition &pos = context.pos(localIdx);
        if (isTumor_(pos))
            return tortuosityTumor_;
        else
            return tortuosityTissue_;
    }

    /*!
     * \brief Define the dispersivity.
     *
     * \param element The finite element
     * \param fvElemGeom The finite volume geometry
     * \param scvIdx The local index of the sub-control volume where
     */
    template <class Context>
    Scalar dispersivity(const Context &context,
                        int localIdx) const
    {
        return 0;
    }

    template <class Context>  
    bool useTwoPointGradient(const Context &context,
                             int scvfIdx) const
    {
        const auto &scvf = context.fvElemGeom().subContVolFace[scvfIdx];
        bool inTumorI = isTumor_(context.pos(scvf.i));
        bool inTumorJ = isTumor_(context.pos(scvf.j));
        
        return (inTumorI && !inTumorJ) || (inTumorJ && !inTumorI);
    }

private:
    bool isTumor_(const GlobalPosition &globalPos) const
    {
        if(10e-3 < globalPos[0] && globalPos[0] < 15e-3 &&
           10e-3 < globalPos[1] && globalPos[1] < 15e-3)
            return true;
        return false;
    }

    Scalar permTumor_;
    Scalar permTissue_;
    Scalar porosityTumor_;
    Scalar porosityTissue_;
    Scalar tortuosityTumor_;
    Scalar tortuosityTissue_;
};

}

#endif
