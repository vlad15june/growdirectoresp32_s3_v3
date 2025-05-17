#ifndef OPTIONAL_H
#define OPTIONAL_H

#include <type_traits>
#include <utility>
#include <stdexcept>

namespace Utils {

// Simple std::optional replacement for platforms lacking full C++17 support
template<typename T>
class Optional {
private:
    bool _hasValue;
    alignas(T) unsigned char _storage[sizeof(T)];
    
    T* _value() {
        return reinterpret_cast<T*>(_storage);
    }
    
    const T* _value() const {
        return reinterpret_cast<const T*>(_storage);
    }
    
public:
    // Default constructor - empty optional
    Optional() : _hasValue(false) {}
    
    // Constructor from value
    Optional(const T& value) : _hasValue(true) {
        new (_storage) T(value);
    }
    
    // Move constructor from value
    Optional(T&& value) : _hasValue(true) {
        new (_storage) T(std::move(value));
    }
    
    // Copy constructor
    Optional(const Optional& other) : _hasValue(other._hasValue) {
        if (_hasValue) {
            new (_storage) T(*other._value());
        }
    }
    
    // Move constructor
    Optional(Optional&& other) : _hasValue(other._hasValue) {
        if (_hasValue) {
            new (_storage) T(std::move(*other._value()));
        }
        other._hasValue = false;
    }
    
    // Destructor
    ~Optional() {
        if (_hasValue) {
            _value()->~T();
        }
    }
    
    // Assignment operators
    Optional& operator=(const Optional& other) {
        if (this != &other) {
            if (_hasValue) {
                _value()->~T();
            }
            
            _hasValue = other._hasValue;
            if (_hasValue) {
                new (_storage) T(*other._value());
            }
        }
        return *this;
    }
    
    Optional& operator=(Optional&& other) {
        if (this != &other) {
            if (_hasValue) {
                _value()->~T();
            }
            
            _hasValue = other._hasValue;
            if (_hasValue) {
                new (_storage) T(std::move(*other._value()));
                other._hasValue = false;
            }
        }
        return *this;
    }
    
    // Check if value exists
    bool has_value() const {
        return _hasValue;
    }
    
    // Boolean conversion
    explicit operator bool() const {
        return _hasValue;
    }
    
    // Access operators
    T& value() {
        if (!_hasValue) {
            throw std::runtime_error("Accessing value of empty Optional");
        }
        return *_value();
    }
    
    const T& value() const {
        if (!_hasValue) {
            throw std::runtime_error("Accessing value of empty Optional");
        }
        return *_value();
    }
    
    // Access with default value
    T value_or(const T& defaultValue) const {
        return _hasValue ? *_value() : defaultValue;
    }
    
    // Dereference operators
    T& operator*() {
        return value();
    }
    
    const T& operator*() const {
        return value();
    }
    
    T* operator->() {
        return &value();
    }
    
    const T* operator->() const {
        return &value();
    }
    
    // Reset the optional
    void reset() {
        if (_hasValue) {
            _value()->~T();
            _hasValue = false;
        }
    }
};

// Helper function to create empty optional
template<class T>
Optional<T> nullopt() {
    return Optional<T>();
}

} // namespace Utils

#endif // OPTIONAL_H
