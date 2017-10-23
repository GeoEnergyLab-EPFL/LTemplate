/*
 * Copyright (c) 2017 Szabolcs Horvát.
 *
 * See the file LICENSE.txt for copying permission.
 */

/**
 * \mainpage
 *
 * This is Doxygen-generated documentation for the C++ interface of the LTemplate _Mathematica_ package.
 *
 * For the latest version of the package go to https://github.com/szhorvat/LTemplate
 *
 * See `LTemplateTutorial.nb` for an introduction and additional documentation.
 *
 * Many commented examples can be found in the `LTemplate/Documentation/Examples` directory.
 */

#ifndef LTEMPLATE_H
#define LTEMPLATE_H

/** \file
 * \brief Include this header before classes to be used with the LTemplate _Mathematica_ package.
 *
 */

#include "LTemplateCompilerSetup.h"

#ifndef LTEMPLATE_USE_CXX11
#error LTemplate requires a compiler with C++11 support.
#endif

#include "mathlink.h"
#include "WolframLibrary.h"
#include "WolframSparseLibrary.h"
#include "WolframImageLibrary.h"

#ifdef LTEMPLATE_RAWARRAY
#include "WolframRawArrayLibrary.h"
#endif

// mathlink.h defines P. It has a high potential for conflict, so we undefine it.
// It is normally only used with .tm files and it is not needed for LTemplate.
#undef P

#include <cstdint>
#include <complex>
#include <string>
#include <ostream>
#include <sstream>
#include <vector>
#include <type_traits>
#include <iterator>
#include <initializer_list>

/// The namespace used by LTemplate
namespace mma {

/// Global `WolframLibraryData` object for accessing the LibraryLink API.
extern WolframLibraryData libData;

/// Complex double type for RawArrays.
typedef std::complex<double> complex_double_t;

/// Complex float type for RawArrays.
typedef std::complex<float>  complex_float_t;

/** \brief Complex number type for Tensors. Alias for \ref mma::complex_double_t.
 *  Same as \c std::complex<double>, thus it can be used with arithmetic operators.
 */
typedef complex_double_t complex_t;

/// For use in the \ref mma::message() function.
enum MessageType { M_INFO, M_WARNING, M_ERROR, M_ASSERT };


/** \brief Issue a _Mathematica_ message.
 *  \param msg the text of the message
 *  \param type determines the message tag which will be used
 *
 * If `msg == NULL`, no message will be issued. This is for compatibility with other libraries
 * that may return a null pointer instead of message text.
 */
void message(const char *msg, MessageType type = M_INFO);

inline void message(std::string msg, MessageType type = M_INFO) { message(msg.c_str(), type); }


/// Call _Mathematica_'s `Print[]`.
inline void print(const char *msg) {
    if (libData->AbortQ())
        return; // trying to use the MathLink connection during an abort appears to break it

    MLINK link = libData->getMathLink(libData);
    MLPutFunction(link, "EvaluatePacket", 1);
        MLPutFunction(link, "Print", 1);
            MLPutString(link, msg);
    libData->processMathLink(link);
    int pkt = MLNextPacket(link);
    if (pkt == RETURNPKT)
        MLNewPacket(link);
}

/// Call _Mathematica_'s `Print[]`, `std::string` argument version.
inline void print(std::string msg) { print(msg.c_str()); }


/// Can be used to output with _Mathematica_'s `Print[]` in a manner similar to `std::cout`. The stream _must_ be flushed (`std::endl` or `std::flush`) to trigger printing.
extern std::ostream mout;


/** \brief Throwing this type returns to _Mathematica_ immediately.
 *  \param s reported in _Mathematica_ as `LTemplate::error`; gets copied.
 *  \param err used as the LibraryFunction exit code.
 */
class LibraryError {
    const std::string msg;
    const bool has_msg;
    const int err_code;

public:
    explicit LibraryError(int err = LIBRARY_FUNCTION_ERROR) : has_msg(false), err_code(err) { }
    LibraryError(std::string s, int err = LIBRARY_FUNCTION_ERROR) : msg(s), has_msg(true), err_code(err) { }

    const std::string &message() const { return msg; }
    bool has_message() const { return has_msg; }
    int error_code() const { return err_code; }

    void report() const {
        if (has_msg)
            mma::message(msg, M_ERROR);
    }
};


#ifdef NDEBUG
#define massert(condition) ((void)0)
#else
/** \brief Replacement for the standard `assert` macro. Instead of aborting the process, it throws a mma::LibraryError
 *
 * As with the standard `assert` macro, define `NDEBUG` to disable assertion checks.
 * LTemplate uses massert() internally in a few places. It can be disabled this way
 * for a minor performance boost.
 */
#define massert(condition) (void)(((condition) || mma::detail::massert_impl(#condition, __FILE__, __LINE__)), 0)
#endif

namespace detail { // private
    [[ noreturn ]] inline bool massert_impl(const char *cond, const char *file, int line)
    {
        std::ostringstream msg;
        msg << cond << ", file " << file << ", line " << line;
        message(msg.str(), M_ASSERT);
        throw LibraryError();
    }
} // end namespace detail


/// Check for and honour user aborts.
inline void check_abort() {
    if (libData->AbortQ())
        throw LibraryError();
}


/// Convenience function for disowning `const char *` strings.
inline void disownString(const char *str) {
    libData->UTF8String_disown(const_cast<char *>(str));
}



///////////////////////////////////////  DENSE AND SPARSE ARRAY HANDLING  ///////////////////////////////////////

namespace detail { // private
    template<typename T> T * getData(MTensor t);

    template<> inline mint * getData(MTensor t) { return libData->MTensor_getIntegerData(t); }
    template<> inline double * getData(MTensor t) { return libData->MTensor_getRealData(t); }
    template<> inline complex_t * getData(MTensor t) { return reinterpret_cast< complex_t * >( libData->MTensor_getComplexData(t) ); }

    // copy data from column major format to row major format
    template<typename T, typename U>
    inline void transposedCopy(const T *from, U *to, mint nrow, mint ncol) {
        for (mint i=0; i < ncol; ++i)
            for (mint j=0; j < nrow; ++j)
                to[i + j*ncol] = from[j + i*nrow];
    }
} // end namespace detail


namespace detail { // private
    template<typename T> inline mint libraryType() {
        static_assert(std::is_same<T, T&>::value,
            "Only mint, double and mma::complex_t are allowed in mma::TensorRef<...> and mma::SparseArrayRef<...>.");
    }

    template<> inline mint libraryType<mint>()      { return MType_Integer; }
    template<> inline mint libraryType<double>()    { return MType_Real; }
    template<> inline mint libraryType<complex_t>() { return MType_Complex; }
} // end namespace detail


template<typename T> class SparseArrayRef;


/** \brief Wrapper class for `MTensor` pointers
 *  \tparam T must be `mint`, `double` or `mma::complex_t`.
 *
 * Note that just like `MTensor`, this class only holds a reference to a Tensor.
 * Multiple \ref TensorRef objects may refer to the same Tensor.
 *
 * \sa MatrixRef, CubeRef
 * \sa makeVector(), makeMatrix(), makeCube()
 */
template<typename T>
class TensorRef {
    const MTensor t; // reminder: MTensor is a pointer type
    T * const tensor_data;
    const mint len;

    TensorRef & operator = (const TensorRef &) = delete;

    // A "null" TensorRef is used only for SparseArrayRef's ev member to handle pattern arrays
    // It cannot be publicly constructed
    TensorRef() : t(nullptr), tensor_data(nullptr), len(0) { }
    bool nullQ() const { return t == nullptr; }

    friend class SparseArrayRef<T>;

public:
    TensorRef(const MTensor &mt) :
        t(mt),
        tensor_data(detail::getData<T>(t)),
        len(libData->MTensor_getFlattenedLength(t))
    {
        detail::libraryType<T>(); // causes compile time error if T is invalid
    }

    /// Returns the referenced \c MTensor
    MTensor tensor() const { return t; }

    /// Returns the rank of the Tensor, same as \c MTensor_getRank
    mint rank() const { return libData->MTensor_getRank(t); }

    /// Returns the number of elements in the Tensor, same as \c MTensor_getFlattenedLength
    mint length() const { return len; }

    /// Returns the number of elements in the Tensor, synonym of \ref length()
    mint size() const { return length(); }

    /// Frees the referenced Tensor, same as \c MTensor_free
    /**
     * Tensors created by the library with functions such as \ref makeVector() must be freed
     * after use unless they are returned to _Mathematica_.
     *
     * Warning: multiple \ref TensorRef objects may reference the same \c MTensor.
     * Freeing the \c MTensor invalidates all references to it.
     */
    void free() const { libData->MTensor_free(t); }

    void disown() const { libData->MTensor_disown(t); }
    void disownAll() const { libData->MTensor_disownAll(t); }

    mint shareCount() const { return libData->MTensor_shareCount(t); }

    /// Creates a copy of the referenced Tensor
    TensorRef clone() const {
        MTensor c = NULL;
        int err = libData->MTensor_clone(t, &c);
        if (err) throw LibraryError("MTensor_clone() failed.", err);
        return c;
    }

    const mint *dimensions() const { return libData->MTensor_getDimensions(t); }

    /// Returns a pointer to the underlying storage of the corresponding \c MTensor
    T *data() const { return tensor_data; }

    T & operator [] (mint i) const { return tensor_data[i]; }

    T *begin() const { return data(); }
    T *end() const { return begin() + length(); }

    mint type() const { return detail::libraryType<T>(); }

    /** Convert to the given type of Tensor
     *  \tparam U is the element type of the result.
     */
    template<typename U>
    TensorRef<U> convertTo() const {
        MTensor mt;
        int err = libData->MTensor_new(detail::libraryType<U>(), rank(), dimensions(), &mt);
        if (err) throw LibraryError("MTensor_new() failed.", err);
        TensorRef<U> tr(mt);
        std::copy(begin(), end(), tr.begin());
        return tr;
    }

    /// Create a new SparseArray from the Tensor data
    SparseArrayRef<T> toSparseArray() const {
        MSparseArray sa = NULL;
        int err = libData->sparseLibraryFunctions->MSparseArray_fromMTensor(t, NULL, &sa);
        if (err) throw LibraryError("MSparseArray_fromMTensor() failed.", err);
        return sa;
    }
};

typedef TensorRef<mint>      IntTensorRef;
typedef TensorRef<double>    RealTensorRef;
typedef TensorRef<complex_t> ComplexTensorRef;


/// Wrapper class for `MTensor` pointers to rank-2 tensors
/**
 * Remember that \c MTensor stores data in row-major order.
 *
 * \sa TensorRef, CubeRef
 * \sa makeMatrix()
 */
template<typename T>
class MatrixRef : public TensorRef<T> {
    mint nrows, ncols;

public:
    MatrixRef(const TensorRef<T> &tr) : TensorRef<T>(tr)
    {
        if (TensorRef<T>::rank() != 2)
            throw LibraryError("MatrixRef: Matrix expected.");
        const mint *dims = TensorRef<T>::dimensions();
        nrows = dims[0];
        ncols = dims[1];
    }

    /// Number of rows in the matrix
    mint rows() const { return nrows; }

    /// Number of columns in the matrix
    mint cols() const { return ncols; }

    /// Index into a matrix using row and column indices
    T & operator () (mint i, mint j) const { return (*this)[ncols*i + j]; }
};

typedef MatrixRef<mint>       IntMatrixRef;
typedef MatrixRef<double>     RealMatrixRef;
typedef MatrixRef<complex_t>  ComplexMatrixRef;


/** \brief Wrapper class for `MTensor` pointers to rank-3 tensors
 *
 * \sa TensorRef, MatrixRef
 * \sa makeCube()
 */
template<typename T>
class CubeRef : public TensorRef<T> {
    mint nslices, nrows, ncols;

public:
    CubeRef(const TensorRef<T> &tr) : TensorRef<T>(tr)
    {
        if (TensorRef<T>::rank() != 3)
            throw LibraryError("CubeRef: Rank-3 tensor expected.");
        const mint *dims = TensorRef<T>::dimensions();
        nslices = dims[0];
        nrows   = dims[1];
        ncols   = dims[2];
    }

    /// Number of rows in the cube
    mint rows() const { return nrows; }

    /// Number of columns in the cube
    mint cols() const { return ncols; }

    /// Number of slices in the cube
    mint slices() const { return nslices; }

    /// Index into a cube using slicem row, and column indices
    T & operator () (mint i, mint j, mint k) const { return (*this)[i*nrows*ncols + j*ncols + k]; }
};

typedef CubeRef<mint>       IntCubeRef;
typedef CubeRef<double>     RealCubeRef;
typedef CubeRef<complex_t>  ComplexCubeRef;


/// Create a Tensor of the given dimensions
template<typename T>
inline TensorRef<T> makeTensor(std::initializer_list<mint> dims) {
    MTensor t = NULL;
    int err = libData->MTensor_new(detail::libraryType<T>(), dims.size(), dims.begin(), &t);
    if (err) throw LibraryError("MTensor_new() failed.", err);
    return t;
}

/** \brief Create a Tensor of the given dimensions.
 *  \param rank is the Tensor depth
 *  \param dims are the dimensions stored in a C array of length \c rank and type \c mint
 *  \tparam T is the type of the Tensor, can be `mint`, `double` or `mma::m_complex`.
 */
template<typename T>
inline TensorRef<T> makeTensor(mint rank, mint *dims) {
    MTensor t = NULL;
    int err = libData->MTensor_new(detail::libraryType<T>(), rank, dims, &t);
    if (err) throw LibraryError("MTensor_new() failed.", err);
    return t;
}

/** \brief Create a Tensor of the given dimensions.
 *  \param rank is the Tensor depth
 *  \param dims are the dimensions stored in a C array of length \c rank and type \c U
 */
template<typename T, typename U>
inline TensorRef<T> makeTensor(mint rank, const U *dims) {
    std::vector<mint> d(dims, dims+rank);
    return makeTensor<T>(rank, d.data());
}


/** \brief Creates a vector (rank-1 Tensor) of the given length
 * \param len is the vector length
 * \tparam T is the Tensor type, can be `mint`, `double` or `mma::m_complex`
 */
template<typename T>
inline TensorRef<T> makeVector(mint len) {
    return makeTensor<T>({len});
}

/// Creates a vector (rank-1 Tensor) of the given length and copies the contents of a C array into it
template<typename T, typename U>
inline TensorRef<T> makeVector(mint len, const U *data) {
    TensorRef<T> t = makeVector<T>(len);
    std::copy(data, data+len, t.begin());
    return t;
}

/// Creates a vector (rank-1 Tensor) from an intializer list
template<typename T>
inline TensorRef<T> makeVector(std::initializer_list<T> l) {
    TensorRef<T> t = makeVector<T>(l.size());
    std::copy(l.begin(), l.end(), t.begin());
    return t;
}


/** \brief Creates a matrix (rank-2 Tensor) of the given dimensions
 * \param nrow is the number of rows
 * \param ncol is the number of columns
 * \tparam T is the type of the Tensor, can be `mint`, `double` or `mma::m_complex`
 */
template<typename T>
inline MatrixRef<T> makeMatrix(mint nrow, mint ncol) {
    return makeTensor<T>({nrow, ncol});
}

/// Creates a matrix (rank-2 Tensor) of the given dimensions and copies the contents of a row-major storage C array into it
template<typename T, typename U>
inline MatrixRef<T> makeMatrix(mint nrow, mint ncol, const U *data) {
    MatrixRef<T> t = makeMatrix<T>(nrow, ncol);
    std::copy(data, data + t.size(), t.begin());
    return t;
}

/// Creates  matrix (rank-2 Tensor) using from a nested intializer list.
template<typename T>
inline MatrixRef<T> makeMatrix(std::initializer_list<std::initializer_list<T>> m) {
    MatrixRef<T> t = makeMatrix<T>(m.size(), m.size() ? m.begin()->size() : 0);
    T *ptr = t.data();
    for (const auto &row : m) {
        massert(row.size() == t.cols());
        for (const auto &el : row) {
            *ptr = el;
            ptr++;
        }
    }
    return t;
}

/// Creates a matrix (rank-2 Tensor) of the given dimensions and copies the contents of a column-major storage C array into it
template<typename T, typename U>
inline MatrixRef<T> makeMatrixTransposed(mint nrow, mint ncol, const U *data) {
    TensorRef<T> t = makeMatrix<T>(nrow, ncol);
    detail::transposedCopy(data, t.data(), nrow, ncol);
    return t;
}


/** \brief Creates a rank-3 Tensor of the given dimensions
 * \param nslice is the number of slices
 * \param nrow is the number of rows
 * \param ncol is the number of columns
 * \tparam T is the type of the Tensor, can be `mint`, `double` or `mma::m_complex`
 */
template<typename T>
inline CubeRef<T> makeCube(mint nslice, mint nrow, mint ncol) {
    return makeTensor<T>({nslice, nrow, ncol});
}

/// Creates a rank-3 Tensor of the given dimensions and copies the contents of a C array into it
template<typename T, typename U>
inline CubeRef<T> makeCube(mint nslice, mint nrow, mint ncol, const U *data) {
    CubeRef<T> t = makeCube<T>(nslice, nrow, ncol);
    std::copy(data, data + t.size(), t.begin());
    return t;
}

/// Creates a rank-3 Tensor from a nested initializer list
template<typename T>
inline CubeRef<T> makeCube(std::initializer_list<std::initializer_list<std::initializer_list<T>>> c) {
    size_t ns = c.size();
    size_t rs = ns ? c.begin()->size() : 0;
    size_t cs = rs ? c.begin()->begin()->size() : 0;
    CubeRef<T> t = makeCube<T>(ns, rs, cs);
    T *ptr = t.data();
    for (const auto &slice : c) {
        massert(slice.size() == rs);
        for (const auto &row : slice) {
            massert(row.size() == cs);
            for (const auto &el : row){
                *ptr = el;
                ptr++;
            }
        }
    }
    return t;
}


template<typename T> class SparseMatrixRef;

/** \brief Wrapper class for `MSparseArray` pointers
 *
 * \sa SparseMatrixRef
 * \sa makeSparseArray(), makeSparseMatrix()
 */
template<typename T>
class SparseArrayRef {
    const MSparseArray sa; // reminder: MSparseArray is a pointer type
    const IntTensorRef rp; // row pointers
    const IntTensorRef ci; // column indices
    const TensorRef<T> ev; // explicit values, ev.nullQ() may be true
    T &iv;                 // implicit value

    SparseArrayRef & operator = (const SparseArrayRef &) = delete;   

    static TensorRef<T> getExplicitValues(const MSparseArray &msa) {
        MTensor *ev = libData->sparseLibraryFunctions->MSparseArray_getExplicitValues(msa);
        if (*ev == NULL)
            return TensorRef<T>();
        else
            return TensorRef<T>(*ev);
    }

    static IntTensorRef getColumnIndices(const MSparseArray &msa) {
        MTensor *ci = libData->sparseLibraryFunctions->MSparseArray_getColumnIndices(msa);

        // Ensure that sparse arrays always have a (possibly empty) column indices vector
        if (*ci == NULL) {
            mint dims[2] = {0, libData->sparseLibraryFunctions->MSparseArray_getRank(msa)};
            libData->MTensor_new(MType_Integer, 2, dims, ci);
        }

        return *ci;
    }

    static T &getImplicitValue(const MSparseArray &msa) {
        MTensor *mt = libData->sparseLibraryFunctions->MSparseArray_getImplicitValue(msa);
        return *(detail::getData<T>(*mt));
    }

    friend class SparseMatrixRef<T>;

public:
    SparseArrayRef(const MSparseArray &msa) :
        sa(msa),
        rp(*(libData->sparseLibraryFunctions->MSparseArray_getRowPointers(msa))),
        ci(getColumnIndices((msa))),
        ev(getExplicitValues(msa)),
        iv(getImplicitValue(msa))
    {
        detail::libraryType<T>(); // causes compile time error if T is invalid
    }

    MSparseArray sparseArray() const { return sa; }

    mint rank() const { return libData->sparseLibraryFunctions->MSparseArray_getRank(sa); }

    const mint *dimensions() const { return libData->sparseLibraryFunctions->MSparseArray_getDimensions(sa); }

    /// The number of explicitly stored positions
    mint length() const { return ci.length(); }

    /// The number of explicitly stored positions, alias for length()
    mint size() const { return length(); }

    void free() const { libData->sparseLibraryFunctions->MSparseArray_free(sa); }
    void disown() const { libData->sparseLibraryFunctions->MSparseArray_disown(sa); }
    void disownAll() const { libData->sparseLibraryFunctions->MSparseArray_disownAll(sa); }

    mint shareCount() const { return libData->sparseLibraryFunctions->MSparseArray_shareCount(sa); }

    /// Creates a copy of the referenced SparseArray
    SparseArrayRef clone() const {
        MSparseArray c = NULL;
        int err = libData->sparseLibraryFunctions->MSparseArray_clone(sa, &c);
        if (err) throw LibraryError("MSparseArray_clone() failed.", err);
        return c;
    }

    /** \brief  Creates a new integer Tensor containing the indices of non-default (i.e. explicit) values in the sparse array.
     *
     *  You are responsible for freeing this data structure using the TensorRef::free() function when done using it.
     */
    IntTensorRef explicitPositions() const {
        MTensor mt = NULL;
        int err = libData->sparseLibraryFunctions->MSparseArray_getExplicitPositions(sa, &mt);
        if (err) throw LibraryError("MSParseArray_getExplicitPositions() failed.", err);

        // Workaround for MSparseArray_getExplicitPositions() returning a non-empty rank-0 MTensor
        // when the SparseArray has no explicit positions: in this case we manually construct
        // a rank-2 0-by-n empty integer MTensor and return that instead.
        if (libData->MTensor_getRank(mt) == 0) {
            libData->MTensor_free(mt);
            return makeMatrix<mint>(0, rank());
        }
        else {
            return IntTensorRef(mt);
        }
    }

    /** \brief Returns the column indices of the SparseArray's internal CSR representation, as an integer Tensor.
     *
     * This function is useful when converting a SparseArray for use with another library that also
     * uses a CSR or CSC representation.
     *
     * The result is either a rank-2 Tensor or an empty one. The indices are 1-based.
     *
     * The result `MTensor` is part of the `MSparseArray` data structure and will be destroyed at the same time with it.
     * Clone it before returning it to the kernel using \ref clone().
     */
    IntTensorRef columnIndices() const {
        return ci;
    }

    /** \brief Returns the row pointers of the SparseArray's internal CSR representation, as a rank-1 integer Tensor.
     *
     * This function is useful when converting a SparseArray for use with another library that also
     * uses a CSR or CSC representation.
     *
     * The result `MTensor` is part of the `MSparseArray` data structure and will be destroyed at the same time with it.
     * Clone it before returning it to the kernel using \ref clone().
     */
    IntTensorRef rowPointers() const { return rp; }

    /// True if the sparse array has explicit values.  Pattern arrays do not have explicit values.
    bool explicitValuesQ() const { return ! ev.nullQ(); }

    /** \brief Returns the explicit values in the sparse array as a Tensor.
     *
     * The result `MTensor` is part of the `MSparseArray` data structure and will be destroyed at the same time with it.
     * Clone it before returning it to the kernel using \ref clone().
     *
     * For pattern arrays a \ref LibraryError exception is thrown.
     */
    TensorRef<T> explicitValues() const {
        if (ev.nullQ())
            throw LibraryError("SparseArrayRef::explicitValues() called on pattern array.");
        return ev;
    }

    /// Returns the background element of the sparse array
    T &implicitValue() const { return iv; }

    /** \brief Creates a new SparseArray in which explicitly stored values that are equal to the current implicit value are eliminated.
     *
     * Should not be used on a pattern array.
     */
    SparseArrayRef resetImplicitValue() const {
        MSparseArray msa = NULL;
        int err = libData->sparseLibraryFunctions->MSparseArray_resetImplicitValue(sa, NULL, &msa);
        if (err) throw LibraryError("MSparseArray_resetImplicitValue() failed.", err);
        return msa;
    }

    /** \brief Creates a new SparseArray based on a new implicit value
     *  \param iv is the new implicit value
     */
    SparseArrayRef resetImplicitValue(const T &iv) const {
        MSparseArray msa = NULL;

        MTensor it = NULL;
        int err = libData->MTensor_new(detail::libraryType<T>(), 0, NULL, &it);
        if (err) throw LibraryError("MTensor_new() failed.", err);
        *detail::getData<T>(it) = iv;

        err = libData->sparseLibraryFunctions->MSparseArray_resetImplicitValue(sa, it, &msa);
        if (err) throw LibraryError("MSparseArray_resetImplicitValue() failed.", err);

        libData->MTensor_free(it);

        return msa;
    }

    /// Creates a new dense Tensor containing the same elements as the sparse array
    TensorRef<T> toTensor() const {
        MTensor t = NULL;
        int err = libData->sparseLibraryFunctions->MSparseArray_toMTensor(sa, &t);
        if (err) throw LibraryError("MSparseArray_toMTensor() failed.", err);
        return t;
    }

    /// Returns the element type of the sparse array
    mint type() const { return detail::libraryType<T>(); }
};


/** \brief Wrapper class for rank-2 SparseArrays
 *
 * \sa SparseArrayRef
 * \sa makeSparseMatrix()
 */
template<typename T>
class SparseMatrixRef : public SparseArrayRef<T> {
    mint ncols, nrows;

    using SparseArrayRef<T>::rp;
    using SparseArrayRef<T>::ci;
    using SparseArrayRef<T>::ev;
    using SparseArrayRef<T>::iv;

public:
    using SparseArrayRef<T>::rank;
    using SparseArrayRef<T>::dimensions;
    using SparseArrayRef<T>::size;
    using SparseArrayRef<T>::explicitValuesQ;

    /// Bidirectional iterator for enumerating the explicitly stored values and positions of a sparse matrix.
    class iterator : public std::iterator<std::bidirectional_iterator_tag, T> {
        const SparseMatrixRef &sm;
        mint row_index, index;

        friend class SparseMatrixRef;

        iterator(const SparseMatrixRef &sm, const mint &row_index, const mint &index) :
            sm(sm),
            row_index(row_index),
            index(index)
        { /* empty */ }

    public:

        iterator(const iterator &) = default;

        /** \brief Access explicit value.
         *
         * Should not be used with pattern arrays. There is no safety check for this.
         */
        T &operator *() const { return sm.ev[index]; }

        bool operator == (const iterator &it) const { return index == it.index; }
        bool operator != (const iterator &it) const { return index != it.index; }

        iterator &operator ++ () {
            index++;
            while (sm.rp[row_index+1] == index && row_index < sm.size())
                row_index++;
            return *this;
        }

        iterator operator ++ (int) {
            iterator it = *this;
            operator++();
            return it;
        }

        iterator &operator -- () {
            while (sm.rp[row_index] == index && row_index > 0)
                row_index--;
            index--;
            return *this;
        }

        iterator operator -- (int) {
            iterator it = *this;
            operator--();
            return it;
        }

        mint row() const { return row_index; } ///< Row of the referenced element (0-based indexing)
        mint col() const { return sm.ci[index]-1; } ///< Column of the referenced element (0-based indexing)
    };

    SparseMatrixRef(const SparseArrayRef<T> &sa) : SparseArrayRef<T>(sa)
    {
        if (rank() != 2)
            throw LibraryError("SparseMatrixRef: Matrix expected.");
        const mint *dims = dimensions();
        nrows = dims[0];
        ncols = dims[1];
    }

    /// Number of rows in the sparse matrix
    mint rows() const { return nrows; }

    /// Number of columns in the sparse matrix
    mint cols() const { return ncols; }

    /** \brief Index into a sparse matrix (read-only, 0-based)
     *
     * This operator provides read access only.
     * For write access to explicit values, use explicitValues().
     */
    T operator () (mint i, mint j) const {
        if (! explicitValuesQ())
            throw LibraryError("SparseMatrixRef: cannot index into a pattern array.");

        // if (i,j) is explicitly stored, it must be located between
        // the following array indices in ev and ci:
        mint lower = rp[i];
        mint upper = rp[i+1];

        // look for the index j between those locations:
        mint *cp = std::lower_bound(&ci[lower], &ci[upper], j+1);
        if (cp == &ci[upper]) // no bound found
            return iv;
        else if (*cp == j+1)  // found a bound equal to the sought column index
            return ev[lower + (cp - &ci[lower])];
        else                  // column index not found
            return iv;
    }

    /** \brief Iterator to beginning of explicit values and positions
     *
     *  If you only need explicit values, not explicit positions, use `sparseArray.explicitValues().begin()` instead.
     *
     * \sa SparseMatrixRef::iterator
     */
    iterator begin() const {
        mint row_index = 0;
        while (rp[row_index+1] == 0 && row_index < size())
            row_index++;
        return iterator{*this, row_index, 0};
    }

    /// Iterator to the end of explicit values and positions
    iterator end() const {
        return iterator{*this, rows(), size()};
    }
};


/** \brief Create a new SparseArray from a set of positions and values.
 *
 * \param pos is the list of explicitly stored positions using 1-based indexing.
 * \param vals is the list of explicitly stored values.
 * \param dims is a list of the sparse array dimensions.
 * \param imp is the implicit value.
 */
template<typename T>
inline SparseArrayRef<T> makeSparseArray(IntMatrixRef pos, TensorRef<T> vals, IntTensorRef dims, T imp = 0) {
    int err;

    massert(pos.cols() == dims.size());
    massert(pos.rows() == vals.size());

    MTensor it = NULL;
    err = libData->MTensor_new(detail::libraryType<T>(), 0, NULL, &it);
    if (err)
        throw LibraryError("makeSparseArray: MTensor_new() failed.", err);
    *detail::getData<T>(it) = imp;

    MSparseArray sa = NULL;
    err = libData->sparseLibraryFunctions->MSparseArray_fromExplicitPositions(pos.tensor(), vals.tensor(), dims.tensor(), it, &sa);
    libData->MTensor_free(it);
    if (err)
        throw LibraryError("makeSparseArray: MSparseArray_fromExplicitPositions() failed.", err);

    // MSparseArray_fromExplicitPositions() will return a pattern array when the positions array is empty.
    // When this happens, we manually insert an explicit values array to ensure that this function
    // never returns a pattern array.
    MTensor *ev;
    ev = libData->sparseLibraryFunctions->MSparseArray_getExplicitValues(sa);
    if (*ev == NULL) {
        mint evdims[1] = {0};
        libData->MTensor_new(detail::libraryType<T>(), 1, evdims, ev);
    }

    return sa;
}

/** \brief Create a new sparse matrix from a set of positions and values.
 *
 * \param pos is the list of explicitly stored positions using 1-based indexing.
 * \param vals is the list of explicitly stored values.
 * \param nrow is the number of matrix rows.
 * \param ncol is the number of matrix columns.
 * \param imp is the implicit value.
 */
template<typename T>
inline SparseMatrixRef<T> makeSparseMatrix(IntMatrixRef pos, TensorRef<T> vals, mint nrow, mint ncol, T imp = 0) {
    massert(pos.cols() == 2);

    IntTensorRef dims = makeVector<mint>({nrow, ncol});
    SparseMatrixRef<T> sa = makeSparseArray(pos, vals, dims, imp);
    dims.free();

    return sa;
}


//////////////////////////////////////////  RAW ARRAY HANDLING  //////////////////////////////////////////

#ifdef LTEMPLATE_RAWARRAY

namespace detail { // private
    template<typename T> inline rawarray_t libraryRawType() {
        static_assert(std::is_same<T, T&>::value,
            "Only int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double, complex_float_t, complex_double_t are allowed in mma::RawArrayRef<...>.");
    }

    template<> inline rawarray_t libraryRawType<int8_t>()   { return MRawArray_Type_Bit8;   }
    template<> inline rawarray_t libraryRawType<uint8_t>()  { return MRawArray_Type_Ubit8;  }
    template<> inline rawarray_t libraryRawType<int16_t>()  { return MRawArray_Type_Bit16;  }
    template<> inline rawarray_t libraryRawType<uint16_t>() { return MRawArray_Type_Ubit16; }
    template<> inline rawarray_t libraryRawType<int32_t>()  { return MRawArray_Type_Bit32;  }
    template<> inline rawarray_t libraryRawType<uint32_t>() { return MRawArray_Type_Ubit32; }
    template<> inline rawarray_t libraryRawType<int64_t>()  { return MRawArray_Type_Bit64;  }
    template<> inline rawarray_t libraryRawType<uint64_t>() { return MRawArray_Type_Ubit64; }
    template<> inline rawarray_t libraryRawType<float>()    { return MRawArray_Type_Real32; }
    template<> inline rawarray_t libraryRawType<double>()   { return MRawArray_Type_Real64; }
    template<> inline rawarray_t libraryRawType<complex_float_t>()  { return MRawArray_Type_Float_Complex; }
    template<> inline rawarray_t libraryRawType<complex_double_t>() { return MRawArray_Type_Double_Complex; }

    inline const char *rawTypeMathematicaName(rawarray_t rt) {
        switch (rt) {
        case MRawArray_Type_Ubit8:          return "UnsignedInteger8";
        case MRawArray_Type_Bit8:           return "Integer8";
        case MRawArray_Type_Ubit16:         return "UnsignedInteger16";
        case MRawArray_Type_Bit16:          return "Integer16";
        case MRawArray_Type_Ubit32:         return "UnsignedInteger32";
        case MRawArray_Type_Bit32:          return "Integer32";
        case MRawArray_Type_Ubit64:         return "UnsignedInteger64";
        case MRawArray_Type_Bit64:          return "Integer64";
        case MRawArray_Type_Real32:         return "Real32";
        case MRawArray_Type_Real64:         return "Real64";
        case MRawArray_Type_Float_Complex:  return "Complex32";
        case MRawArray_Type_Double_Complex: return "Complex64";
        case MRawArray_Type_Undef:          return "Undefined";
        default:                            return "Unknown"; // should never reach here
        }
    }

} // end namespace detail


template<typename T> class RawArrayRef;


/// Wrapper class for `MRawArray` pointers; unspecialized base class. Typically used through \ref RawArrayRef.
class GenericRawArrayRef {
    const MRawArray ra;
    const mint len;

    GenericRawArrayRef & operator = (const GenericRawArrayRef &) = delete;

public:
    GenericRawArrayRef(const MRawArray &mra) :
        ra(mra),
        len(libData->rawarrayLibraryFunctions->MRawArray_getFlattenedLength(mra))
    { }

    /// Returns the referenced \c MRawArray
    MRawArray rawArray() const { return ra; }

    /// Returns the rank of the RawArray, same as \c MRawArray_getRank
    mint rank() const { return libData->rawarrayLibraryFunctions->MRawArray_getRank(ra); }

    /// Returns the number of elements in the RawArray, same as \c MRawArray_getFlattenedLength
    mint length() const { return len; }

    /// Returns the number of elements in the RawArray, synonym of \ref length()
    mint size() const { return length(); }

    /// Frees the referenced RawArray, same as \c MRawArray_free
    /**
     * Warning: multiple \ref RawArrayRef objects may reference the same \c MRawArray.
     * Freeing the \c MRawArray invalidates all references to it.
     */
    void free() const { libData->rawarrayLibraryFunctions->MRawArray_free(ra); }

    void disown() const { libData->rawarrayLibraryFunctions->MRawArray_disown(ra); }
    void disownAll() const { libData->rawarrayLibraryFunctions->MRawArray_disownAll(ra); }

    mint shareCount() const { return libData->rawarrayLibraryFunctions->MRawArray_shareCount(ra); }

    const mint *dimensions() const { return libData->rawarrayLibraryFunctions->MRawArray_getDimensions(ra); }

    template<typename U>
    RawArrayRef<U> convertTo() const {
        // TODO check error?
        return libData->rawarrayLibraryFunctions->MRawArray_convertType(ra, detail::libraryRawType<U>());
    }
};


/// Wrapper class for `MRawArray` pointers.
template<typename T>
class RawArrayRef : public GenericRawArrayRef {
    T * const array_data;

public:
    RawArrayRef(const MRawArray &mra) :
        GenericRawArrayRef(mra),
        array_data(reinterpret_cast<T *>(libData->rawarrayLibraryFunctions->MRawArray_getData(mra)))
    {
        rawarray_t received = libData->rawarrayLibraryFunctions->MRawArray_getType(mra);
        rawarray_t expected = detail::libraryRawType<T>();
        if (received != expected) {
            std::ostringstream err;
            err << "RawArray of type " << detail::rawTypeMathematicaName(received) << " received, "
                << detail::rawTypeMathematicaName(expected) << " expected.";
            throw LibraryError(err.str(), LIBRARY_TYPE_ERROR);
        }
    }

    /// Creates a copy of the referenced RawArray
    RawArrayRef clone() const {
        MRawArray c = NULL;
        int err = libData->rawarrayLibraryFunctions->MRawArray_clone(rawArray(), &c);
        if (err) throw LibraryError("MRawArray_clone() failed.", err);
        return c;
    }

    /// Returns a pointer to the underlying storage of the corresponding \c MRawArray
    T *data() const { return array_data; }

    T & operator [] (mint i) const { return array_data[i]; }

    T *begin() const { return data(); }
    T *end() const { return begin() + length(); }

    rawarray_t type() const { return detail::libraryRawType<T>(); }
};

/// Creates a RawArray of the given dimensions
template<typename T>
inline RawArrayRef<T> makeRawArray(std::initializer_list<mint> l) {
    MRawArray ra = NULL;
    int err = libData->rawarrayLibraryFunctions->MRawArray_new(detail::libraryRawType<T>(), l.size(), l.begin(), &ra);
    if (err) throw LibraryError("MRawArray_new() failed.", err);
    return ra;
}

/** \brief Create a RawArray of the given dimensions.
 *  \param rank is the RawArray depth
 *  \param dims are the dimensions stored in a C array of length \c rank and type \c mint
 */
template<typename T>
inline RawArrayRef<T> makeRawArray(mint rank, const mint *dims) {
    MRawArray ra = NULL;
    int err = libData->rawarrayLibraryFunctions->MRawArray_new(detail::libraryRawType<T>(), rank, dims, &ra);
    if (err) throw LibraryError("MRawArray_new() failed.", err);
    return ra;
}

/** \brief Create a RawArray of the given dimensions.
 *  \param rank is the RawArray depth
 *  \param dims are the dimensions stored in a C array of length \c rank and type \c U
 */
template<typename T, typename U>
inline RawArrayRef<T> makeRawArray(mint rank, const U *dims) {
    std::vector<mint> d(dims, dims+rank);
    return makeRawArray<T>(rank, d.data());
}

/// Creates a rank-1 RawArray of the given length
template<typename T>
inline RawArrayRef<T> makeRawVector(mint len) {
    return makeRawArray<T>({len});
}

#endif // LTEMPLATE_RAWARRAY



//////////////////////////////////////////  IMAGE HANDLING  //////////////////////////////////////////


/* While the C++ standard does not guarantee that sizeof(bool) == 1, this is currently
 * the case for most implementations. This was verified at https://gcc.godbolt.org/
 * across multiple platforms and architectures in October 2017.
 *
 * The ABIs used by major operating systems also specify a bool or C99 _Bool of size 1
 * https://github.com/rust-lang/rfcs/pull/954#issuecomment-169820630
 *
 * Thus it seems safe to require sizeof(bool) == 1. A safety check is below.
 */
static_assert(sizeof(bool) == 1, "The bool type is expected to be of size 1.");

/* We use a new set of types for image elements. These all correspond to raw_t_... types
 * from WolframImageLibrary.h with the exception of im_bit_t, which is bool.
 * This is so that it will be distinct from im_byte_t.
 */
/// @{
typedef bool            im_bit_t;
typedef unsigned char   im_byte_t;
typedef unsigned short  im_bit16_t;
typedef float           im_real32_t;
typedef double          im_real_t;
/// @}

namespace detail { // private
    template<typename T> inline imagedata_t libraryImageType() {
        static_assert(std::is_same<T, T&>::value,
            "Only im_bit_t, im_byte_t, im_bit16_t, im_real32_t, im_real_t are allowed in mma::ImageRef<...>.");
    }

    template<> inline imagedata_t libraryImageType<im_bit_t>()    { return MImage_Type_Bit;   }
    template<> inline imagedata_t libraryImageType<im_byte_t>()   { return MImage_Type_Bit8;  }
    template<> inline imagedata_t libraryImageType<im_bit16_t>()  { return MImage_Type_Bit16;  }
    template<> inline imagedata_t libraryImageType<im_real32_t>() { return MImage_Type_Real32; }
    template<> inline imagedata_t libraryImageType<im_real_t>()   { return MImage_Type_Real;  }


    inline const char *imageTypeMathematicaName(imagedata_t it) {
        switch (it) {
        case MImage_Type_Bit:       return "Bit";
        case MImage_Type_Bit8:      return "Byte";
        case MImage_Type_Bit16:     return "Bit16";
        case MImage_Type_Real32:    return "Real32";
        case MImage_Type_Real:      return "Real";
        case MImage_Type_Undef:     return "Undefined";
        default:                    return "Unknown"; // should never reach here
        }
    }

} // end namespace detail


template<typename T> class ImageRef;
template<typename T> class Image3DRef;

/// Wrapper class for `MImage` pointers; unspecialized base class. Typically used through \ref ImageRef or \ref Image3DRef.
class GenericImageRef {
    const MImage im;
    const mint len;
    const mint nrows, ncols, nslices, nchannels;
    const bool interleaved, alphaChannel;

    GenericImageRef & operator = (const GenericImageRef &) = delete;

public:
    GenericImageRef(const MImage &mim) :
        im(mim),
        len(libData->imageLibraryFunctions->MImage_getFlattenedLength(im)),
        nrows(libData->imageLibraryFunctions->MImage_getRowCount(im)),
        ncols(libData->imageLibraryFunctions->MImage_getColumnCount(im)),
        nslices(libData->imageLibraryFunctions->MImage_getSliceCount(im)),
        nchannels(libData->imageLibraryFunctions->MImage_getChannels(im)),
        interleaved(libData->imageLibraryFunctions->MImage_interleavedQ(im)),
        alphaChannel(libData->imageLibraryFunctions->MImage_alphaChannelQ(im))
    { }

    /// Returns the referenced \c MImage
    MImage image() const { return im; }

    /// Returns the total number of pixels in all image channels. Same as \c MImage_getFlattenedLength. \sa channelSize
    mint length() const { return len; }

    /// Returns the total number of pixels in all image channels, synonym of \ref length(). \sa channelSize
    mint size() const { return length(); }

    /// Returns the number of image rows
    mint rows() const { return nrows; }

    /// Returns the number of image columns
    mint cols() const { return ncols; }

    /// Returns the number of image slices for 3D images; for 2D images it returns 1.
    mint slices() const { return nslices; }

    /// Returns the number of pixels in a single image channel.
    mint channelSize() const { return slices()*rows()*cols(); }

    /// Returns 2 for 3D images and 3 for 3D images.
    mint rank() const { return libData->imageLibraryFunctions->MImage_getRank(im); }

    /// Returns the number of image channels
    mint channels() const { return nchannels; }

    /// Returns the number of non-alpha channels. Same as \ref channels() if the image has no alpha channel; one less otherwise.
    mint nonAlphaChannels() const { return alphaChannelQ() ? channels()-1 : channels(); }

    /// Returns true if image channels are stored in interleaved mode, e.g. `rgbrgbrgb...` instead of `rrr...ggg...bbb...` for an RGB image.
    bool interleavedQ() const { return interleaved; }

    /// Does the image have an alpha channel?
    bool alphaChannelQ() const { return alphaChannel; }

    /// Returns the image colour space
    colorspace_t colorSpace() const {
        return libData->imageLibraryFunctions->MImage_getColorSpace(im);
    }

    /// Frees the referenced \c MImage, same as \c MImage_free
    void free() const { libData->imageLibraryFunctions->MImage_free(im); }

    void disown() const { libData->imageLibraryFunctions->MImage_disown(im); }
    void disownAll() const { libData->imageLibraryFunctions->MImage_disownAll(im); }

    mint shareCount() const { return libData->imageLibraryFunctions->MImage_shareCount(im); }

    /** \brief Convert the image to the given type of \ref ImageRef
     *  \param interleaving specifies whether to store the data in interleaved mode. See \ref interleavedQ
     *  \tparam U is the pixel type of the result.
     */
    template<typename U>
    ImageRef<U> convertTo(bool interleaving) const {
        return libData->imageLibraryFunctions->MImage_convertType(im, detail::libraryImageType<U>(), interleaving);
    }

    /// Convert the image to the given type of \ref ImageRef. The interleaving mode is preserved.
    template<typename U>
    ImageRef<U> convertTo() const { return convertTo<U>(interleavedQ()); }
};


template<typename T>
class pixel_iterator : public std::iterator<std::random_access_iterator_tag, T> {
    T *ptr;
    const ptrdiff_t step;

    friend ImageRef<T>;
    friend Image3DRef<T>;

    pixel_iterator(T *ptr, ptrdiff_t step) :
        ptr(ptr), step(step)
    { }

public:

    pixel_iterator(const pixel_iterator &) = default;


    bool operator == (const pixel_iterator &it) const { return ptr == it.ptr; }
    bool operator != (const pixel_iterator &it) const { return ptr != it.ptr; }

    T &operator *() const { return *ptr; }

    pixel_iterator &operator ++ () {
        ptr += step;
        return *this;
    }

    pixel_iterator operator ++ (int) {
        pixel_iterator it = *this;
        operator++();
        return it;
    }

    pixel_iterator &operator -- () {
        ptr -= step;
        return *this;
    }

    pixel_iterator operator -- (int) {
        pixel_iterator it = *this;
        operator--();
        return it;
    }

    pixel_iterator operator + (ptrdiff_t n) const { return pixel_iterator(ptr + n*step, step); }

    pixel_iterator operator - (ptrdiff_t n) const { return pixel_iterator(ptr - n*step, step); }

    ptrdiff_t operator - (const pixel_iterator &it) const { (ptr - it.ptr)/step; }

    T & operator [] (mint i) { return ptr[i*step]; }

    bool operator < (const pixel_iterator &it) const { ptr < it.ptr; }
    bool operator > (const pixel_iterator &it) const { ptr > it.ptr; }
    bool operator <= (const pixel_iterator &it) const { ptr <= it.ptr; }
    bool operator >= (const pixel_iterator &it) const { ptr >= it.ptr; }
};


/** \brief Wrapper class for `MImage` pointers referring to 2D images.
 *  \tparam T is the pixel type of the image. It corresponds to _Mathematica_'s `ImageType` as per the table below:
 *
 * `ImageType` | C++ type      |  Alias for
 * ------------|---------------|-------------------
 *  `"Bit"`    | `im_bit_t`    |  `bool`
 *  `"Byte"`   | `im_byte_t`   |  `unsigned char`
 *  `"Bit16"`  | `im_bit16_t`  |  `unsigned short`
 *  `"Real32"` | `im_real32_t` |  `float`
 *  `"Real"`   | `im_real_t`   |  `double`
 *
 * Note that this class only holds a reference to an Image. Multiple \ref ImageRef
 * or \ref GenericImageRef objects may point to the same Image.
 *
 * \sa Image3DRef
 */
template<typename T>
class ImageRef : public GenericImageRef {
    T * const image_data;

public:

    /// Random access iterator for accessing the pixels of a single image channel in order
    typedef class pixel_iterator<T> pixel_iterator;

    ImageRef(const MImage &mim) :
        GenericImageRef(mim),
        image_data(reinterpret_cast<T *>(libData->imageLibraryFunctions->MImage_getRawData(mim)))
    {
        // TODO Is this check necessary? Mathematica  should always convert to the correct image type.
        imagedata_t received = libData->imageLibraryFunctions->MImage_getDataType(mim);
        imagedata_t expected = detail::libraryImageType<T>();
        if (received != expected) {
            std::ostringstream err;
            err << "Image of type " << detail::imageTypeMathematicaName(received) << " received, "
                << detail::imageTypeMathematicaName(expected) << " expected.";
            throw LibraryError(err.str(), LIBRARY_TYPE_ERROR);
        }

        if (GenericImageRef::rank() != 2)
            throw LibraryError("2D image expected.", LIBRARY_TYPE_ERROR);
    }

    /// Returns 2 for a 2D image.
    mint rank() const { return 2; }

    /// Creates a copy of the referenced Image
    ImageRef clone() const {
        MImage c = NULL;
        int err = libData->imageLibraryFunctions->MImage_clone(image(), &c);
        if (err) throw LibraryError("MImage_clone() failed.", err);
        return c;
    }

    /// Pointer to the image data
    T *data() const { return image_data; }

    /// Returns an interator to the beginning of the image data
    T *begin() const { return data(); }

    /// Returns an interator to the end of the image data
    T *end() const { return begin() + length(); }

    /// Returns a pixel iterator to the beginning of \p channel
    pixel_iterator pixelBegin(mint channel) const {
        if (interleavedQ())
            return pixel_iterator(image_data + channel, channels());
        else
            return pixel_iterator(image_data + channelSize()*channel, 1);
    }

    /// Returns an iterator to the end of \p channel
    pixel_iterator pixelEnd(mint channel) const {
        return pixelBegin(channel) + channelSize();
    }

    /// Index into the image
    T &operator ()(mint row, mint col, mint channel = 0) const {
        if (interleavedQ())
            return image_data[row*cols()*channels() + col*channels()+ channel];
        else
            return image_data[channel*rows()*cols() + row*cols() + col];
    }

    /// Returns the image/pixel type
    imagedata_t type() const { return detail::libraryImageType<T>(); }
};


/** \brief Wrapper class for `MImage` pointers referring to 3D images.
 *  \tparam T is the element type of the image. It corresponds to _Mathematica_'s `ImageType` as per the table below:
 *
 * `ImageType` | C++ type      |  Alias for
 * ------------|---------------|-------------------
 *  `"Bit"`    | `im_bit_t`    |  `bool`
 *  `"Byte"`   | `im_byte_t`   |  `unsigned char`
 *  `"Bit16"`  | `im_bit16_t`  |  `unsigned short`
 *  `"Real32"` | `im_real32_t` |  `float`
 *  `"Real"`   | `im_real_t`   |  `double`
 *
 * Note that this class only holds a reference to an Image. Multiple \ref Image3DRef
 * or \ref GenericImageRef objects may point to the same Image.
 *
 * \sa ImageRef
 */
template<typename T>
class Image3DRef : public GenericImageRef {
    T * const image_data;

public:

    /// Random access iterator for accessing the pixels of a single image channel in order
    typedef class pixel_iterator<T> pixel_iterator;

    Image3DRef(const MImage &mim) :
        GenericImageRef(mim),
        image_data(reinterpret_cast<T *>(libData->imageLibraryFunctions->MImage_getRawData(mim)))
    {
        // TODO Is this check necessary? Mathematica  should always convert to the correct image type.
        imagedata_t received = libData->imageLibraryFunctions->MImage_getDataType(mim);
        imagedata_t expected = detail::libraryImageType<T>();
        if (received != expected) {
            std::ostringstream err;
            err << "Image of type " << detail::imageTypeMathematicaName(received) << " received, "
                << detail::imageTypeMathematicaName(expected) << " expected.";
            throw LibraryError(err.str(), LIBRARY_TYPE_ERROR);
        }

        if (GenericImageRef::rank() != 3)
            throw LibraryError("3D image expected.", LIBRARY_TYPE_ERROR);
    }

    /// Returns 3 for a 3D image
    mint rank() const { return 3; }

    /// Creates a copy of the referenced Image3D
    Image3DRef clone() const {
        MImage c = NULL;
        int err = libData->imageLibraryFunctions->MImage_clone(image(), &c);
        if (err) throw LibraryError("MImage_clone() failed.", err);
        return c;
    }

    /// Pointer to the image data
    T *data() const { return image_data; }

    /// Returns an interator to the beginning of the image data
    T *begin() const { return data(); }

    /// Returns an interator to the end of the image data
    T *end() const { return begin() + length(); }

    /// Returns a pixel iterator to the beginning of \p channel
    pixel_iterator pixelBegin(mint channel) const {
        if (interleavedQ())
            return pixel_iterator(image_data + channel, channels());
        else
            return pixel_iterator(image_data + channelSize()*channel, 1);
    }

    /// Returns an iterator to the end of \p channel
    pixel_iterator pixelEnd(mint channel) const {
        return pixelBegin(channel) + channelSize();
    }

    /// Index into the image
    T &operator ()(mint slice, mint row, mint col, mint channel = 0) const {
        if (interleavedQ())
            return image_data[slice*rows()*cols()*channels() + row*cols()*channels() + col*channels() + channel];
        else
            return image_data[channel*slices()*rows()*cols() + slice*rows()*cols() + row*cols() + col];
    }

    /// Returns the image/pixel type
    imagedata_t type() const { return detail::libraryImageType<T>(); }
};


/** \brief Create a new Image
 *  \param width of the image (number of columns)
 *  \param height of the image (number of rows)
 *  \param channels is the number of image channels
 *  \param interleaving specifies whether to store the data in interleaved mode
 *  \param colorspace may be one of `MImage_CS_Automatic`, `MImage_CS_Gray`, `MImage_CS_RGB`, `MImage_CS_HSB`, `MImage_CS_CMYK`, `MImage_CS_XYZ`, `MImage_CS_LUV`, `MImage_CS_LAB`, `MImage_CS_LCH`
 *  \tparam T is the pixel type
 */
template<typename T>
inline ImageRef<T> makeImage(mint width, mint height, mint channels = 1, bool interleaving = true, colorspace_t colorspace = MImage_CS_Automatic) {
    MImage mim;
    libData->imageLibraryFunctions->MImage_new2D(width, height, channels, detail::libraryImageType<T>(), colorspace, interleaving, &mim);
    return mim;
}

/** \brief Create a new Image3D
 *  \param slices is the number of image slices
 *  \param width of individual slices (number of rows)
 *  \param height of individual slices (number of columns)
 *  \param channels is the number of image channels
 *  \param interleaving specifies whether to store the data in interleaved mode
 *  \param colorspace may be one of `MImage_CS_Automatic`, `MImage_CS_Gray`, `MImage_CS_RGB`, `MImage_CS_HSB`, `MImage_CS_CMYK`, `MImage_CS_XYZ`, `MImage_CS_LUV`, `MImage_CS_LAB`, `MImage_CS_LCH`
 *  \tparam T is the pixel type
 *
 */
template<typename T>
inline Image3DRef<T> makeImage3D(mint slices, mint width, mint height, mint channels = 1, bool interleaving = true, colorspace_t colorspace = MImage_CS_Automatic) {
    MImage mim;
    libData->imageLibraryFunctions->MImage_new3D(slices, width, height, channels, detail::libraryImageType<T>(), colorspace, interleaving, &mim);
    return mim;
}

} // end namespace mma

#endif // LTEMPLATE_H

