// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "tensor.h"
#include "logging.h"
#include "math.h"
#include <string>

using namespace std;

namespace ark {

TensorBuf::TensorBuf(const DimType &bytes_, int id_) : bytes{bytes_}, id{id_}
{
}

// Tensor constructor
Tensor::Tensor(const Dims &shape_, TensorType type_, TensorBuf *buf_,
               const Dims &ldims_, const Dims &offs_, const Dims &pads_,
               bool exported_, int imported_rank_, int id_, const string &name_)
    : buf{buf_}, type{type_}, exported{exported_},
      imported_rank{imported_rank_}, id{id_}, name{name_}
{
    if (shape_.size() == 0) {
        LOGERR("Tensor shape should consist of positive numbers. Given: ",
               shape_);
    } else if (shape_.is_no_dim()) {
        // Assume a single-element constant
        this->shape = {1};
    } else {
        this->shape = shape_;
    }
    int ndims = this->shape.ndims();
    if (ldims_.is_no_dim()) {
        this->ldims = this->shape;
    } else {
        if (ndims != ldims_.ndims()) {
            LOGERR("Tensor shape and ldims should have the same number of "
                   "dimensions. Given: shape ",
                   this->shape, " ldims ", ldims_);
        }
        this->ldims = ldims_;
    }
    if (offs_.is_no_dim()) {
        vector<DimType> dims_vec;
        for (int i = 0; i < ndims; ++i) {
            dims_vec.push_back(0);
        }
        this->offs = Dims{dims_vec};
    } else {
        if (ndims != offs_.ndims()) {
            LOGERR("Tensor shape and offs should have the same number of "
                   "dimensions. Given: shape ",
                   this->shape, " offs ", offs_);
        }
        this->offs = offs_;
    }
    if (pads_.is_no_dim()) {
        vector<DimType> dims_vec;
        for (int i = 0; i < ndims; ++i) {
            dims_vec.push_back(1);
        }
        this->pads = Dims{dims_vec};
    } else {
        if (ndims != pads_.ndims()) {
            LOGERR("Tensor shape and pads should have the same number of "
                   "dimensions. Given: shape ",
                   this->shape, " pads ", pads_);
        }
        this->pads = pads_;
    }
    for (int i = 0; i < ndims; ++i) {
        if (this->ldims[i] % this->pads[i] != 0) {
            LOGERR("Tensor ldims should be a multiple of pads. ldims ",
                   this->ldims, " pads ", this->pads);
        }
    }
    for (int i = 0; i < ndims; ++i) {
        if (this->offs[i] + this->shape[i] > this->ldims[i]) {
            LOGERR("Tensor exceeds the memory boundary. offs ", this->offs,
                   " shape ", this->shape, " ldims ", this->ldims);
        }
    }
}

//
void Tensor::update_pads(const vector<DimType> &pads_)
{
    for (auto &p : pads_) {
        if (p <= 0) {
            LOG(ERROR, "Tensor pads should be positive. Given: ", p);
        }
    }
    int ndims = this->ldims.ndims();
    vector<DimType> tmp;
    for (int i = 0; i < ndims - (int)pads_.size(); ++i) {
        tmp.emplace_back(1);
    }
    for (int i = 0; i < (int)pads_.size(); ++i) {
        tmp.emplace_back(pads_[i]);
    }
    Dims new_pads{tmp};
    for (int i = 0; i < ndims; ++i) {
        DimType new_udim = math::lcm(this->pads[i], new_pads[i]);
        this->pads[i] = new_udim;
        this->ldims[i] = math::pad(this->ldims[i], new_udim);
    }
}

// Offset to the element [i0][i1][i2][i3] of this tensor in the TensorBuf.
DimType Tensor::offset(DimType i0, DimType i1, DimType i2, DimType i3) const
{
    auto &l = this->ldims;
    auto &o = this->offs;
    int ndims = this->shape.ndims();
    if (ndims == 0) {
        return 0;
    } else if (ndims == 1) {
        return o[0] + i0;
    } else if (ndims == 2) {
        return ((o[0] + i0) * l[1]) + o[1] + i1;
    } else if (ndims == 3) {
        return ((o[0] + i0) * l[1] * l[2]) + ((o[1] + i1) * l[2]) + o[2] + i2;
    }
    return ((o[0] + i0) * l[1] * l[2] * l[3]) + ((o[1] + i1) * l[2] * l[3]) +
           ((o[2] + i2) * l[3]) + o[3] + i3;
}

// Number of elements in the tensor excluding padding.
DimType Tensor::size() const
{
    return this->shape.size();
}

// Number of dimensions in the tensor.
int Tensor::ndims() const
{
    return this->shape.ndims();
}

// Shape of the tensor including padding.
Dims Tensor::padded_shape() const
{
    std::vector<DimType> padded_shape;
    for (int i = 0; i < this->shape.ndims(); ++i) {
        padded_shape.push_back(math::pad(this->shape[i], this->pads[i]));
    }
    Dims ps{padded_shape};
    return ps;
}

// Number of bytes of each element in the tensor.
unsigned int Tensor::type_bytes() const
{
    if (this->type == FP16) {
        return 2;
    } else if (this->type == FP32) {
        return 4;
    } else if (this->type == INT32) {
        return 4;
    } else if (this->type == BYTE) {
        return 1;
    }
    return 0;
}

// Number of bytes of the tensor.
DimType Tensor::shape_bytes() const
{
    return this->shape.size() * this->type_bytes();
}

// Should be the same as the number of bytes of the TensorBuf.
DimType Tensor::ldims_bytes() const
{
    return this->ldims.size() * this->type_bytes();
}

// Offset in bytes.
DimType Tensor::offset_bytes(DimType i0, DimType i1, DimType i2,
                             DimType i3) const
{
    return this->offset(i0, i1, i2, i3) * this->type_bytes();
}

// TODO: deprecate this function.
bool Tensor::is_sequential() const
{
    // if a tensor's last (ndims-1) shape is the same as its ldims, the tensor
    // is sequential
    int ndims = this->shape.ndims();
    for (int i = 1; i < ndims; ++i) {
        if (this->shape[i] != this->ldims[i]) {
            return false;
        }
    }
    return true;
}

const string type_str(const TensorType &type)
{
    if (type == FP16)
        return "fp16";
    else if (type == FP32)
        return "fp32";
    else if (type == INT32)
        return "int32";
    else if (type == BYTE)
        return "byte";
    return "none";
}

std::ostream &operator<<(std::ostream &os, TensorType type)
{
    os << type_str(type);
    return os;
}

} // namespace ark
