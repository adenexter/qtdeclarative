/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/
#ifndef QV4VALUE_DEF_P_H
#define QV4VALUE_DEF_P_H

#include <QtCore/QString>
#include "qv4global_p.h"

QT_BEGIN_NAMESPACE

namespace QV4 {

typedef uint Bool;

template <typename T>
struct Returned : private T
{
    static Returned<T> *create(T *t) { return static_cast<Returned<T> *>(t); }
    T *getPointer() { return this; }
    template<typename X>
    static T *getPointer(Returned<X> *x) { return x->getPointer(); }
    template<typename X>
    Returned<X> *as() { return Returned<X>::create(Returned<X>::getPointer(this)); }
    using T::asReturnedValue;
};

struct Q_QML_EXPORT Value
{
    /*
        We use two different ways of encoding JS values. One for 32bit and one for 64bit systems.

        In both cases, we 8 bytes for a value and different variant of NaN boxing. A Double NaN (actually -qNaN)
        is indicated by a number that has the top 13 bits set. THe other values are usually set to 0 by the
        processor, and are thus free for us to store other data. We keep pointers in there for managed objects,
        and encode the other types using the free space given to use by the unused bits for NaN values. This also
        works for pointers on 64 bit systems, as they all currently only have 48 bits of addressable memory.

        On 32bit, we store doubles as doubles. All other values, have the high 32bits set to a value that
        will make the number a NaN. The Masks below are used for encoding the other types.

        On 64 bit, we xor Doubles with (0xffff8000 << 32). Thas has the effect that no doubles will get encoded
        with the 13 highest bits all 0. We are now using special values for bits 14-17 to encode our values. These
        can be used, as the highest valid pointer on a 64 bit system is 2^48-1.

        If they are all 0, we have a pointer to a Managed object. If bit 14 is set we have an integer.
        This makes testing for pointers and numbers very fast (we have a number if any of the highest 14 bits is set).

        Bit 15-17 is then used to encode other immediates.
    */


    union {
        quint64 val;
#if QT_POINTER_SIZE == 8
        Managed *m;
        Object *o;
        String *s;
#else
        double dbl;
#endif
        struct {
#if Q_BYTE_ORDER != Q_LITTLE_ENDIAN
            uint tag;
#endif
            union {
                uint uint_32;
                int int_32;
#if QT_POINTER_SIZE == 4
                Managed *m;
                Object *o;
                String *s;
#endif
            };
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            uint tag;
#endif
        };
    };

#if QT_POINTER_SIZE == 4
    enum Masks {
        NaN_Mask = 0x7ff80000,
        NotDouble_Mask = 0x7ffc0000,
        Type_Mask = 0xffff8000,
        Immediate_Mask = NotDouble_Mask | 0x00008000,
        IsNullOrUndefined_Mask = Immediate_Mask | 0x20000,
        Tag_Shift = 32
    };
    enum ValueType {
        Undefined_Type = Immediate_Mask | 0x00000,
        Null_Type = Immediate_Mask | 0x10000,
        Boolean_Type = Immediate_Mask | 0x20000,
        Integer_Type = Immediate_Mask | 0x30000,
        Managed_Type = NotDouble_Mask | 0x00000,
        Empty_Type = NotDouble_Mask | 0x30000
    };

    enum ImmediateFlags {
        ConvertibleToInt = Immediate_Mask | 0x1
    };

    enum ValueTypeInternal {
        _Null_Type = Null_Type | ConvertibleToInt,
        _Boolean_Type = Boolean_Type | ConvertibleToInt,
        _Integer_Type = Integer_Type | ConvertibleToInt,

    };
#else
    static const quint64 NaNEncodeMask = 0xffff800000000000ll;
    static const quint64 IsInt32Mask  = 0x0002000000000000ll;
    static const quint64 IsDoubleMask = 0xfffc000000000000ll;
    static const quint64 IsNumberMask = IsInt32Mask|IsDoubleMask;
    static const quint64 IsNullOrUndefinedMask = 0x0000800000000000ll;
    static const quint64 IsNullOrBooleanMask = 0x0001000000000000ll;
    static const quint64 IsConvertibleToIntMask = IsInt32Mask|IsNullOrBooleanMask;

    enum Masks {
        NaN_Mask = 0x7ff80000,
        Type_Mask = 0xffff8000,
        IsDouble_Mask = 0xfffc0000,
        Immediate_Mask = 0x00018000,
        IsNullOrUndefined_Mask = 0x00008000,
        IsNullOrBoolean_Mask = 0x00010000,
        Tag_Shift = 32
    };
    enum ValueType {
        Undefined_Type = IsNullOrUndefined_Mask,
        Null_Type = IsNullOrUndefined_Mask|IsNullOrBoolean_Mask,
        Boolean_Type = IsNullOrBoolean_Mask,
        Integer_Type = 0x20000|IsNullOrBoolean_Mask,
        Managed_Type = 0,
        Empty_Type = Undefined_Type | 0x4000
    };
    enum {
        IsDouble_Shift = 64-14,
        IsNumber_Shift = 64-15,
        IsConvertibleToInt_Shift = 64-16,
        IsManaged_Shift = 64-17
    };


    enum ValueTypeInternal {
        _Null_Type = Null_Type,
        _Boolean_Type = Boolean_Type,
        _Integer_Type = Integer_Type
    };
#endif

    inline unsigned type() const {
        return tag & Type_Mask;
    }

    // used internally in property
    inline bool isEmpty() const { return tag == Empty_Type; }

    inline bool isUndefined() const { return tag == Undefined_Type; }
    inline bool isNull() const { return tag == _Null_Type; }
    inline bool isBoolean() const { return tag == _Boolean_Type; }
#if QT_POINTER_SIZE == 8
    inline bool isInteger() const { return (val >> IsNumber_Shift) == 1; }
    inline bool isDouble() const { return (val >> IsDouble_Shift); }
    inline bool isNumber() const { return (val >> IsNumber_Shift); }
    inline bool isManaged() const { return !(val >> IsManaged_Shift); }
    inline bool isNullOrUndefined() const { return ((val >> IsManaged_Shift) & ~2) == 1; }
    inline bool integerCompatible() const { return ((val >> IsConvertibleToInt_Shift) & ~2) == 1; }
    static inline bool integerCompatible(Value a, Value b) {
        return a.integerCompatible() && b.integerCompatible();
    }
    static inline bool bothDouble(Value a, Value b) {
        return a.isDouble() && b.isDouble();
    }
    double doubleValue() const {
        Q_ASSERT(isDouble());
        union {
            quint64 i;
            double d;
        } v;
        v.i = val ^ NaNEncodeMask;
        return v.d;
    }
    void setDouble(double d) {
        union {
            quint64 i;
            double d;
        } v;
        v.d = d;
        val = v.i ^ NaNEncodeMask;
        Q_ASSERT(isDouble());
    }
    bool isNaN() const { return (tag & 0x7fff8000) == 0x00078000; }
#else
    inline bool isInteger() const { return tag == _Integer_Type; }
    inline bool isDouble() const { return (tag & NotDouble_Mask) != NotDouble_Mask; }
    inline bool isNumber() const { return tag == _Integer_Type || (tag & NotDouble_Mask) != NotDouble_Mask; }
    inline bool isManaged() const { return tag == Managed_Type; }
    inline bool isNullOrUndefined() const { return (tag & IsNullOrUndefined_Mask) == Undefined_Type; }
    inline bool integerCompatible() const { return (tag & ConvertibleToInt) == ConvertibleToInt; }
    static inline bool integerCompatible(Value a, Value b) {
        return ((a.tag & b.tag) & ConvertibleToInt) == ConvertibleToInt;
    }
    static inline bool bothDouble(Value a, Value b) {
        return ((a.tag | b.tag) & NotDouble_Mask) != NotDouble_Mask;
    }
    double doubleValue() const { return dbl; }
    void setDouble(double d) { dbl = d; }
    bool isNaN() const { return (tag & QV4::Value::NotDouble_Mask) == QV4::Value::NaN_Mask; }
#endif
    inline bool isString() const;
    inline bool isObject() const;
    inline bool isInt32() {
        if (tag == _Integer_Type)
            return true;
        if (isDouble()) {
            double d = doubleValue();
            int i = (int)d;
            if (i == d) {
                int_32 = i;
                tag = _Integer_Type;
                return true;
            }
        }
        return false;
    }
    double asDouble() const {
        if (tag == _Integer_Type)
            return int_32;
        return doubleValue();
    }

    bool booleanValue() const {
        return int_32;
    }
    int integerValue() const {
        return int_32;
    }

    String *stringValue() const {
        return s;
    }
    Object *objectValue() const {
        return o;
    }
    Managed *managed() const {
        return m;
    }

    quint64 rawValue() const {
        return val;
    }

    static inline Value fromManaged(Managed *o);

    int toUInt16() const;
    inline int toInt32() const;
    inline unsigned int toUInt32() const;

    inline bool toBoolean() const;
    double toInteger() const;
    inline double toNumber() const;
    double toNumberImpl() const;
    QString toQStringNoThrow() const;
    QString toQString() const;
    String *toString(ExecutionContext *ctx) const;
    Object *toObject(ExecutionContext *ctx) const;

    inline bool isPrimitive() const;
    inline bool tryIntegerConversion() {
        bool b = integerCompatible();
        if (b)
            tag = _Integer_Type;
        return b;
    }

    inline String *asString() const;
    inline Managed *asManaged() const;
    inline Object *asObject() const;
    inline FunctionObject *asFunctionObject() const;
    inline NumberObject *asNumberObject() const;
    inline StringObject *asStringObject() const;
    inline DateObject *asDateObject() const;
    inline ArrayObject *asArrayObject() const;
    inline ErrorObject *asErrorObject() const;

    template<typename T> inline T *as() const;

    inline uint asArrayIndex() const;
    inline uint asArrayLength(bool *ok) const;

    inline ExecutionEngine *engine() const;

    ReturnedValue asReturnedValue() const { return val; }
    static Value fromReturnedValue(ReturnedValue val) { Value v; v.val = val; return v; }
    Value &operator=(ReturnedValue v) { val = v; return *this; }
    template <typename T>
    inline Value &operator=(Returned<T> *t);

    // Section 9.12
    bool sameValue(Value other) const;

    inline void mark(ExecutionEngine *e) const;
};

inline Managed *Value::asManaged() const
{
    if (isManaged())
        return managed();
    return 0;
}

inline String *Value::asString() const
{
    if (isString())
        return stringValue();
    return 0;
}

struct Q_QML_EXPORT Primitive : public Value
{
    inline static Primitive emptyValue();
    static inline Primitive fromBoolean(bool b);
    static inline Primitive fromInt32(int i);
    inline static Primitive undefinedValue();
    static inline Primitive nullValue();
    static inline Primitive fromDouble(double d);
    static inline Primitive fromUInt32(uint i);

    static double toInteger(double fromNumber);
    static int toInt32(double value);
    static unsigned int toUInt32(double value);

    inline operator ValueRef();
    Value asValue() const { return *this; }
};

inline Primitive Primitive::undefinedValue()
{
    Primitive v;
#if QT_POINTER_SIZE == 8
    v.val = quint64(Undefined_Type) << Tag_Shift;
#else
    v.tag = Undefined_Type;
    v.int_32 = 0;
#endif
    return v;
}

inline Primitive Primitive::emptyValue()
{
    Primitive v;
    v.tag = Value::Empty_Type;
    v.uint_32 = 0;
    return v;
}

inline Value Value::fromManaged(Managed *m)
{
    if (!m)
        return QV4::Primitive::undefinedValue();
    Value v;
#if QT_POINTER_SIZE == 8
    v.m = m;
#else
    v.tag = Managed_Type;
    v.m = m;
#endif
    return v;
}

struct SafeValue : public Value
{
    SafeValue &operator =(const ScopedValue &v);
    template<typename T>
    SafeValue &operator=(Returned<T> *t);
    SafeValue &operator=(ReturnedValue v) {
        val = v;
        return *this;
    }
    template<typename T>
    SafeValue &operator=(T *t) {
        val = Value::fromManaged(t).val;
        return *this;
    }

    template<typename T>
    SafeValue &operator=(const Scoped<T> &t);
    SafeValue &operator=(const ValueRef v);
    SafeValue &operator=(const Value &v) {
        val = v.val;
        return *this;
    }
    template<typename T>
    inline Returned<T> *as();
    template<typename T>
    inline Referenced<T> asRef();
};

template <typename T>
struct Safe : public SafeValue
{
    template<typename X>
    Safe &operator =(X *x) {
        val = Value::fromManaged(x).val;
    }
    Safe &operator =(T *t);
    Safe &operator =(const Scoped<T> &v);
    Safe &operator =(const Referenced<T> &v);
    Safe &operator =(Returned<T> *t);

    Safe &operator =(const Safe<T> &t);

    bool operator!() const { return !managed(); }

    T *operator->() { return static_cast<T *>(managed()); }
    const T *operator->() const { return static_cast<T *>(managed()); }
    T *getPointer() const { return static_cast<T *>(managed()); }
    Returned<T> *ret() const;

    void mark(ExecutionEngine *e) { if (managed()) managed()->mark(e); }
};
typedef Safe<String> SafeString;
typedef Safe<Object> SafeObject;

template<typename T>
T *value_cast(const Value &v)
{
    return v.as<T>();
}

template<typename T>
ReturnedValue value_convert(ExecutionContext *ctx, const Value &v);



}

QT_END_NAMESPACE

#endif // QV4VALUE_DEF_P_H
