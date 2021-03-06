//
//  ShapeInfo.h
//  libraries/physcis/src
//
//  Created by Andrew Meadows 2014.10.29
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ShapeInfo_h
#define hifi_ShapeInfo_h

#include <QVector>
#include <glm/glm.hpp>

#include "DoubleHashKey.h"

enum ShapeType {
    SHAPE_TYPE_NONE,
    SHAPE_TYPE_BOX,
    SHAPE_TYPE_SPHERE,
    SHAPE_TYPE_ELLIPSOID,
    SHAPE_TYPE_HULL,
    SHAPE_TYPE_PLANE,
    SHAPE_TYPE_COMPOUND,
    SHAPE_TYPE_CAPSULE_X,
    SHAPE_TYPE_CAPSULE_Y,
    SHAPE_TYPE_CAPSULE_Z,
    SHAPE_TYPE_CYLINDER_X,
    SHAPE_TYPE_CYLINDER_Y,
    SHAPE_TYPE_CYLINDER_Z
};

class ShapeInfo {
public:
    void clear();

    void setParams(ShapeType type, const glm::vec3& halfExtents, QVector<glm::vec3>* data = NULL);
    void setBox(const glm::vec3& halfExtents);
    void setSphere(float radius);
    void setEllipsoid(const glm::vec3& halfExtents);
    //void setHull(); // TODO: implement this
    void setCapsuleY(float radius, float halfHeight);

    const int getType() const { return _type; }

    const glm::vec3& getHalfExtents() const { return _halfExtents; }

    void setData(const QVector<glm::vec3>* data) { _externalData = data; }
    const QVector<glm::vec3>* getData() const { return _externalData; }

    float computeVolume() const;

    const DoubleHashKey& getHash() const;

protected:
    ShapeType _type = SHAPE_TYPE_NONE;
    glm::vec3 _halfExtents = glm::vec3(0.0f);
    DoubleHashKey _doubleHashKey;
    const QVector<glm::vec3>* _externalData = NULL;
};

#endif // hifi_ShapeInfo_h

