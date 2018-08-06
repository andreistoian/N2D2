/*
    (C) Copyright 2016 CEA LIST. All Rights Reserved.
    Contributor(s): Olivier BICHLER (olivier.bichler@cea.fr)

    This software is governed by the CeCILL-C license under French law and
    abiding by the rules of distribution of free software.  You can  use,
    modify and/ or redistribute the software under the terms of the CeCILL-C
    license as circulated by CEA, CNRS and INRIA at the following URL
    "http://www.cecill.info".

    As a counterpart to the access to the source code and  rights to copy,
    modify and redistribute granted by the license, users are provided only
    with a limited warranty  and the software's author,  the holder of the
    economic rights,  and the successive licensors  have only  limited
    liability.

    The fact that you are presently reading this means that you have had
    knowledge of the CeCILL-C license and that you accept its terms.
*/

#ifndef N2D2_SGDSOLVER_FRAME_H
#define N2D2_SGDSOLVER_FRAME_H

#include "Solver/SGDSolver.hpp"

namespace N2D2 {
template <class T> class SGDSolver_Frame : public SGDSolver {
public:
    static std::shared_ptr<SGDSolver> create()
    {
        return std::make_shared<SGDSolver_Frame<T> >();
    }

    SGDSolver_Frame();
    SGDSolver_Frame(const SGDSolver_Frame<T>& solver);
    void update(BaseTensor& data, BaseTensor& diffData, unsigned int batchSize);
    void exportFreeParameters(const std::string& fileName) const;
    std::shared_ptr<SGDSolver_Frame<T> > clone() const
    {
        return std::shared_ptr<SGDSolver_Frame<T> >(doClone());
    }
    virtual ~SGDSolver_Frame() {};

protected:
    /// Quantization levels (0 = no quantization)
    Parameter<unsigned int> mQuantizationLevels;

    Tensor<T> mMomentumData;
    Tensor<T> mContinuousData;

private:
    virtual SGDSolver_Frame<T>* doClone() const
    {
        return new SGDSolver_Frame<T>(*this);
    }

    static Registrar<SGDSolver> mRegistrar;
};
}

template <class T>
N2D2::SGDSolver_Frame<T>::SGDSolver_Frame()
    : SGDSolver(),
      mQuantizationLevels(this, "QuantizationLevels", 0U)
{
    // ctor
}

template <class T>
N2D2::SGDSolver_Frame<T>::SGDSolver_Frame(const SGDSolver_Frame<T>& solver)
    : SGDSolver(solver),
      mQuantizationLevels(this, "QuantizationLevels",
                          solver.mQuantizationLevels)
{
    // copy-ctor
}

template <class T>
void N2D2::SGDSolver_Frame<T>::update(BaseTensor& baseData,
                                      BaseTensor& baseDiffData,
                                      unsigned int batchSize)
{
    Tensor<T>& data = dynamic_cast<Tensor<T>&>(baseData);
    Tensor<T>& diffData = dynamic_cast<Tensor<T>&>(baseDiffData);

    const T rate(SGDSolver::getLearningRate(batchSize));

    if (rate == 0.0)
        return;

    if (mQuantizationLevels > 0 && mContinuousData.empty()) {
        mContinuousData.resize(data.dims());
        std::copy(data.begin(), data.end(), mContinuousData.begin());
    }

    Tensor<T>& continuousData
        = (mQuantizationLevels > 0) ? mContinuousData : data;

    // Normalize in function of the iteration size
    const T rateDiff(rate / (batchSize * (T)mIterationSize));

    if (mMomentum == 0.0 && mDecay == 0.0) {
        // if outside the loop for better performance
        if (mClamping) {
            //#pragma omp parallel for
            for (int index = 0; index < (int)data.size(); ++index) {
                continuousData(index) = Utils::clamp<T>(
                    continuousData(index)
                        + rateDiff * diffData(index), T(-1.0), T(1.0));
            }
        } else {
            //#pragma omp parallel for
            for (int index = 0; index < (int)data.size(); ++index)
                continuousData(index) += rateDiff * diffData(index);
        }
    } else {
        const T momentum(mMomentum);

        if (mMomentumData.empty())
            mMomentumData.resize(data.dims(), T(0.0));

#pragma omp parallel for if (mMomentumData.size() > 1024)
        for (int index = 0; index < (int)mMomentumData.size(); ++index) {
            // mMomentumData = mMomentumData*momentum
            mMomentumData(index) *= momentum;

            // mMomentumData = mMomentumData + diffData*mWeightsLearningRate
            mMomentumData(index) += rateDiff * diffData(index);

            if (mDecay != 0.0) {
                const T decay(mDecay);
                const T alpha = -decay * rate;

                // mMomentumData = mMomentumData - decay*rate*data
                mMomentumData(index) += alpha * continuousData(index);
            }

            // data = data + mMomentumData
            if (mClamping)
                continuousData(index) = Utils::clamp
                    <T>(continuousData(index) + mMomentumData(index),
                        T(-1.0), T(1.0));
            else
                continuousData(index) += mMomentumData(index);
        }
    }

    if (mQuantizationLevels > 0) {
        //#pragma omp parallel for
        for (int index = 0; index < (int)data.size(); ++index) {
            data(index) = (mQuantizationLevels > 1)
               ? (int)Utils::round((mQuantizationLevels - 1)
                 * continuousData(index)) / (T)(mQuantizationLevels - 1)
               : ((continuousData(index) >= 0) ? 1 : -1);
        }
    }
}

template <class T>
void N2D2::SGDSolver_Frame<T>::exportFreeParameters(const std::string& fileName)
    const
{
    if (mMomentum != 0.0) {
        std::ofstream syn(fileName.c_str());

        if (!syn.good())
            throw std::runtime_error("Could not create synaptic file : "
                                     + fileName);

        for (typename std::vector<T>::const_iterator it = mMomentumData.begin();
             it != mMomentumData.end();
             ++it)
            syn << (*it) << " ";

        if (!syn.good())
            throw std::runtime_error("Error writing synaptic file: "
                                     + fileName);
    }
}

#endif // N2D2_SGDSOLVER_FRAME_H
