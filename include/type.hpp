#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace lumen {

enum class TypeKind {
    Int32,
    Char8,
    Void,
    Pointer,
    Array,
    NullPointer,
};

class Type final {
public:
    using Ptr = std::shared_ptr<const Type>;

    static Ptr int32() {
        static const Ptr instance(new Type(TypeKind::Int32));
        return instance;
    }

    static Ptr char8() {
        static const Ptr instance(new Type(TypeKind::Char8));
        return instance;
    }

    static Ptr voidType() {
        static const Ptr instance(new Type(TypeKind::Void));
        return instance;
    }

    static Ptr pointerTo(Ptr pointee) {
        return Ptr(new Type(TypeKind::Pointer, std::move(pointee)));
    }

    static Ptr arrayOf(Ptr element, std::size_t length) {
        return Ptr(new Type(TypeKind::Array, std::move(element), length));
    }

    static Ptr nullPointer() {
        static const Ptr instance(new Type(TypeKind::NullPointer));
        return instance;
    }

    TypeKind kind() const { return kind_; }
    const Ptr& elementType() const { return elementType_; }
    std::size_t arrayLength() const { return arrayLength_; }

    bool equals(const Type& other) const {
        if (kind_ != other.kind_ || arrayLength_ != other.arrayLength_) return false;
        if (elementType_ && other.elementType_) return elementType_->equals(*other.elementType_);
        return !elementType_ && !other.elementType_;
    }

    std::string toString() const {
        switch (kind_) {
        case TypeKind::Int32:
            return "int";
        case TypeKind::Char8:
            return "char";
        case TypeKind::Void:
            return "void";
        case TypeKind::Pointer:
            return elementType_->toString() + "*";
        case TypeKind::Array:
            return elementType_->toString() + "[" + std::to_string(arrayLength_) + "]";
        case TypeKind::NullPointer:
            return "null";
        }
        return "<invalid-type>";
    }

private:
    explicit Type(TypeKind kind, Ptr element = nullptr, std::size_t length = 0)
        : kind_(kind), elementType_(std::move(element)), arrayLength_(length) {}

    TypeKind kind_;
    Ptr elementType_;
    std::size_t arrayLength_ = 0;
};

using TypePtr = Type::Ptr;

inline bool sameType(const TypePtr& left, const TypePtr& right) {
    return left && right && left->equals(*right);
}

inline bool isPointerType(const TypePtr& type) {
    return type && type->kind() == TypeKind::Pointer;
}

inline bool isArrayType(const TypePtr& type) {
    return type && type->kind() == TypeKind::Array;
}

} // namespace lumen
