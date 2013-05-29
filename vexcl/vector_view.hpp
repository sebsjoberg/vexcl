#ifndef VEXCL_VECTOR_VIEW_HPP
#define VEXCL_VECTOR_VIEW_HPP

/*
The MIT License

Copyright (c) 2012-2013 Denis Demidov <ddemidov@ksu.ru>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/**
 * \file   vexcl/vector_view.hpp
 * \author Denis Demidov <ddemidov@ksu.ru>
 * \brief  Provides sub-view for an existing vex::vector.
 */

#include <array>
#include <numeric>
#include <algorithm>
#include <vexcl/vector.hpp>

namespace vex {

struct vector_view_terminal {};

typedef vector_expression<
    typename boost::proto::terminal< vector_view_terminal >::type
    > vector_view_terminal_expression;

template <typename T, class Slice>
struct vector_view : public vector_view_terminal_expression
{
    const vector<T> &base;
    const Slice     &slice;

    vector_view(const vector<T> &base, const Slice &slice)
        : base(base), slice(slice)
    { }
};

// Allow vector_view to participate in vector expressions:
template <>
struct is_vector_expr_terminal< vector_view_terminal >
    : std::true_type
{ };

template <typename T, class Slice>
struct kernel_name< vector_view<T, Slice> > {
    static std::string get() {
        return "view_";
    }
};

template <typename T, class Slice>
struct partial_vector_expr< vector_view<T, Slice> > {
    static std::string get(int component, int position) {
        return Slice::partial_expression(component, position);
    }
};

template <typename T, class Slice>
struct terminal_preamble< vector_view<T, Slice> > {
    static std::string get(int component, int position) {
        return Slice::indexing_function(component, position);
    }
};

template <typename T, class Slice>
struct kernel_param_declaration< vector_view<T, Slice> > {
    static std::string get(int component, int position) {
        return Slice::template parameter_declaration<T>(component, position);
    }
};

template <typename T, class Slice>
struct kernel_arg_setter< vector_view<T, Slice> > {
    static void set(cl::Kernel &kernel, uint device, size_t index_offset, uint &position, const vector_view<T, Slice> &term) {
        assert(device == 0);

        Slice::setArgs(kernel, device, index_offset, position, term);
    }
};

/// Generalized slice selector.
/**
 * This is very similar to std::gslice.
 *
 * Index to base vector is obtained as start + sum(i_k * stride[k]), where i_k
 * is coordinate along each dimension of gslice.
 */
template <size_t NDIM>
struct gslice {
    static_assert(NDIM > 0, "Incorrect dimension for gslice");

    cl_ulong start;
    cl_ulong size[NDIM];
    cl_long  stride[NDIM]; // Signed type allows reverse slicing.

#ifndef BOOST_NO_INITIALIZER_LISTS
    template <typename T1, typename T2>
    gslice(cl_ulong start,
           const std::initializer_list<T1> &p_size,
           const std::initializer_list<T2> &p_stride
          ) : start(start)
    {
        assert(p_size.size()   == NDIM);
        assert(p_stride.size() == NDIM);

        std::copy(p_size.begin(),   p_size.end(),   size);
        std::copy(p_stride.begin(), p_stride.end(), stride);
    }
#endif

    template <typename T1, typename T2>
    gslice(cl_ulong start,
           const std::array<T1, NDIM> &p_size,
           const std::array<T2, NDIM> &p_stride
          ) : start(start)
    {
        std::copy(p_size.begin(),   p_size.end(),   size);
        std::copy(p_stride.begin(), p_stride.end(), stride);
    }

    template <typename T1, typename T2>
    gslice(cl_ulong start,
           const T1 *p_size,
           const T2 *p_stride
          ) : start(start)
    {
        std::copy(p_size,   p_size   + NDIM, size);
        std::copy(p_stride, p_stride + NDIM, stride);
    }

    static std::string indexing_function(int component, int position) {
        std::ostringstream s;

        s << "ulong slice_" << component << "_" << position << "(\n\tulong start";
        for(size_t k = 0; k < NDIM; ++k)
            s << ",\n\tulong size" << k << ",\n\tlong stride" << k;
        s << ",\n\tulong idx)\n{\n";

        if (NDIM == 1) {
            s << "    return start + idx * stride0;\n";
        } else {
            s << "    size_t ptr = start + (idx % size" << NDIM - 1 <<  ") * stride" << NDIM - 1 << ";\n";
            for(size_t k = NDIM - 1; k-- > 0;) {
                s << "    idx /= size" << k + 1 << ";\n"
                     "    ptr += (idx % size" << k <<  ") * stride" << k <<  ";\n";
            }
            s << "    return ptr;\n";
        }
        s << "}\n\n";

        return s.str();
    }

    static std::string partial_expression(int component, int position) {
        std::ostringstream prm;
        prm << "prm_" << component << "_" << position << "_";

        std::ostringstream s;

        s << prm.str() << "base["
          << "slice_" << component << "_" << position << "("
          << prm.str() << "start";
        for(size_t k = 0; k < NDIM; ++k)
            s << ", " << prm.str() << "size"   << k
              << ", " << prm.str() << "stride" << k;
        s << ", idx)]";

        return s.str();
    }

    template <typename T>
    static std::string parameter_declaration(int component, int position) {
        std::ostringstream prm;
        prm << "prm_" << component << "_" << position << "_";

        std::ostringstream s;

        s << "global " << type_name<T>() << " * " << prm.str() << "base"
          << ", ulong " << prm.str() << "start";

        for(size_t k = 0; k < NDIM; ++k)
            s << ", ulong " << prm.str() << "size"   << k
              << ", long  " << prm.str() << "stride" << k;

        return s.str();
    }

    template <typename T>
    static void setArgs(cl::Kernel &kernel, uint device, size_t/*index_offset*/, uint &position, const vector_view<T, gslice> &term) {
        kernel.setArg(position++, term.base(device));
        kernel.setArg(position++, term.slice.start);
        for(size_t k = 0; k < NDIM; ++k) {
            kernel.setArg(position++, term.slice.size[k]);
            kernel.setArg(position++, term.slice.stride[k]);
        }
    }

    template <typename T>
    vector_view<T, gslice> operator()(const vector<T> &base) const {
        assert(start + (size[0] - 1) * stride[0] < base.size());
        return vector_view<T, gslice>(base, *this);
    }
};


struct range {
    size_t start;
    size_t stride;
    size_t stop;

    range(size_t start, size_t stride, size_t stop)
        : start(start), stride(stride), stop(stop) {}

    range(size_t start, size_t stop)
        : start(start), stride(1), stop(stop) {}
};

template <size_t NDIM>
class slicer {
    private:
        std::array<size_t, NDIM> dim;
    public:
        template <size_t CDIM>
        struct slice : public gslice<NDIM> {
            std::array<size_t, NDIM> dim;

#ifndef BOOST_NO_INITIALIZER_LISTS
            template <typename T1, typename T2>
            slice(size_t start,
                  const std::initializer_list<T1> &size,
                  const std::initializer_list<T2> &stride,
                  const std::array<size_t, NDIM> &dim
                 ) : gslice<NDIM>(start, size, stride), dim(dim) {}
#endif

            template <typename T1, typename T2>
            slice(size_t start,
                  const std::array<T1, NDIM> &size,
                  const std::array<T2, NDIM> &stride,
                  const std::array<size_t, NDIM> &dim
                 ) : gslice<NDIM>(start, size, stride), dim(dim) {}

            template <typename T1, typename T2>
            slice(size_t start,
                  const T1 *size,
                  const T2 *stride,
                  const std::array<size_t, NDIM> &dim
                 ) : gslice<NDIM>(start, size, stride), dim(dim) {}

            slice(const slice<CDIM - 1> &parent, const range &r)
                : gslice<NDIM>(parent.start, parent.size, parent.stride), dim(parent.dim)
            {
                this->start += r.start * std::accumulate(
                        dim.begin() + CDIM + 1, dim.end(), 1UL, std::multiplies<size_t>());

                this->size[CDIM] = (r.stop - r.start + r.stride - 1) / r.stride;
                this->stride[CDIM] = r.stride;
            }

            slice<CDIM + 1> operator[](const range &r) const {
                static_assert(CDIM + 1 < NDIM, "Incorrect dimensions in vex::slicer[]");

                return slice<CDIM + 1>(*this, r);
            };
        };

#ifndef BOOST_NO_INITIALIZER_LISTS
        template <typename T>
        slicer(const std::initializer_list<T> &target_dimensions) {
            std::copy(target_dimensions.begin(), target_dimensions.end(), dim.begin());
        }
#endif
        template <typename T>
        slicer(const std::array<T, NDIM> &target_dimensions) {
            std::copy(target_dimensions.begin(), target_dimensions.end(), dim.begin());
        }

        template <typename T>
        slicer(const T *target_dimensions) {
            std::copy(target_dimensions, target_dimensions + NDIM, dim.begin());
        }

        slice<0> operator[](const range &r) const {
            size_t start = r.start * std::accumulate(
                    dim.begin() + 1, dim.end(), 1UL, std::multiplies<size_t>());

            std::array<size_t, NDIM> size = {{(r.stop - r.start + r.stride - 1) / r.stride}};
            std::copy(dim.begin() + 1, dim.end(), size.begin() + 1);

            std::array<size_t, NDIM> stride = {{r.stride}};
            std::fill(stride.begin() + 1, stride.end(), 1);

            return slice<0>(start, size, stride, dim);
        }
};


} // namespace vex

#endif