#include <cmath>
#include "jointnodetree.h"

IMPL_JointSideToString
IMPL_JointSideFromString
IMPL_JointSideToDispName

JointNodeTree::JointNodeTree(MeshResultContext &resultContext)
{
    const BmeshNode *rootNode = resultContext.centerBmeshNode();
    if (!rootNode) {
        return;
    }
    
    isVerticalSpine = resultContext.isVerticalSpine();
    
    JointInfo rootCenterJoint;
    {
        rootCenterJoint.jointIndex = m_tracedJoints.size();
        QMatrix4x4 localMatrix;
        rootCenterJoint.partId = rootNode->bmeshId;
        rootCenterJoint.nodeId = rootNode->nodeId;
        rootCenterJoint.position = rootNode->origin;
        rootCenterJoint.boneMark = rootNode->boneMark;
        rootCenterJoint.scale = QVector3D(1.0, 1.0, 1.0);
        rootCenterJoint.radius = rootNode->radius;
        m_tracedJoints.push_back(rootCenterJoint);
    }
    
    //qDebug() << "Root node partId:" << rootNode->bmeshId << "nodeId:" << rootNode->nodeId;
    
    std::set<std::pair<int, int>> visitedNodes;
    std::set<std::pair<std::pair<int, int>, std::pair<int, int>>> connections;
    m_tracedNodeToJointIndexMap[std::make_pair(rootNode->bmeshId, rootNode->nodeId)] = rootCenterJoint.jointIndex;
    traceBoneFromJoint(resultContext, std::make_pair(rootNode->bmeshId, rootNode->nodeId), visitedNodes, connections, rootCenterJoint.jointIndex);
    
    calculateMatricesByPosition();
    
    collectParts();
}

const std::vector<std::vector<int>> &JointNodeTree::legs() const
{
    return m_legs;
}

const std::vector<std::pair<std::vector<int>, std::vector<int>>> &JointNodeTree::pairedLegs() const
{
    return m_pairedLegs;
}

const std::vector<int> &JointNodeTree::spine() const
{
    return m_spine;
}

void JointNodeTree::getMarkedNodeList(std::vector<JointMarkedNode> &markedNodes, std::vector<int> *jointIndicies) const
{
    if (-1 != head) {
        JointMarkedNode mn;
        mn.position = joints()[head].position;
        mn.boneMark = joints()[head].boneMark;
        markedNodes.push_back(mn);
        if (nullptr != jointIndicies)
            jointIndicies->push_back(head);
    }
    for (size_t i = 0; i < pairedLegs().size(); i++) {
        const auto &leftLeg = pairedLegs()[i].first;
        if (!leftLeg.empty()) {
            const auto &leg = leftLeg;
            JointMarkedNode mn;
            for (size_t j = 0; j < leg.size(); j++) {
                int jointIndex = leg[j];
                mn.jointSide = JointSide::Left;
                mn.siblingOrder = i;
                mn.jointOrder = j;
                mn.position = joints()[jointIndex].position;
                mn.boneMark = joints()[jointIndex].boneMark;
                markedNodes.push_back(mn);
                if (nullptr != jointIndicies)
                    jointIndicies->push_back(jointIndex);
            }
        }
        const auto &rightLeg = pairedLegs()[i].second;
        if (!rightLeg.empty()) {
            const auto &leg = rightLeg;
            JointMarkedNode mn;
            for (size_t j = 0; j < leg.size(); j++) {
                int jointIndex = leg[j];
                mn.jointSide = JointSide::Right;
                mn.siblingOrder = i;
                mn.jointOrder = j;
                mn.position = joints()[jointIndex].position;
                mn.boneMark = joints()[jointIndex].boneMark;
                markedNodes.push_back(mn);
                if (nullptr != jointIndicies)
                    jointIndicies->push_back(jointIndex);
            }
        }
    }
    if (-1 != tail) {
        JointMarkedNode mn;
        mn.position = joints()[tail].position;
        mn.boneMark = joints()[tail].boneMark;
        markedNodes.push_back(mn);
        if (nullptr != jointIndicies)
            jointIndicies->push_back(tail);
    }
}

void JointNodeTree::clearParts()
{
    head = -1;
    tail = -1;
    m_spine.clear();
    m_legs.clear();
    m_pairedLegs.clear();
}

void JointNodeTree::collectParts()
{
    clearParts();
    
    std::vector<std::vector<int>> leftLegs;
    std::vector<std::vector<int>> rightLegs;
    for (const auto &node: joints()) {
        if (node.boneMark == SkeletonBoneMark::Spine) {
            m_spine.push_back(node.jointIndex);
        }
        if (node.boneMark == SkeletonBoneMark::Head) {
            if (-1 != head) {
                qDebug() << "Get multiple head markers";
            }
            head = node.jointIndex;
        }
        if (node.boneMark == SkeletonBoneMark::Tail) {
            if (-1 != tail) {
                qDebug() << "Get multiple tail markers";
            }
            tail = node.jointIndex;
        }
        if (node.boneMark == SkeletonBoneMark::LegStart && node.children.size() == 1) {
            const auto legStart = node.jointIndex;
            const JointInfo *loopNode = &joints()[node.children[0]];
            std::vector<int> trace;
            trace.push_back(legStart);
            while (loopNode->boneMark != SkeletonBoneMark::LegEnd &&
                    loopNode->children.size() == 1) {
                trace.push_back(loopNode->jointIndex);
                loopNode = &joints()[loopNode->children[0]];
            }
            if (loopNode->boneMark == SkeletonBoneMark::LegEnd) {
                const auto legEnd = loopNode->jointIndex;
                trace.push_back(legEnd);
                if (loopNode->position.x() > 0)
                    leftLegs.push_back(trace);
                else
                    rightLegs.push_back(trace);
            } else {
                qDebug() << "Find leg" << node.partId << "'s end failed";
            }
        }
    }
    sortSpine(m_spine);
    
    sortLegs(leftLegs);
    sortLegs(rightLegs);
    size_t unionIndex = 0;
    for (; unionIndex < leftLegs.size() && unionIndex < rightLegs.size(); unionIndex++) {
        m_legs.push_back(leftLegs[unionIndex]);
        m_legs.push_back(rightLegs[unionIndex]);
        m_pairedLegs.push_back(std::make_pair(leftLegs[unionIndex], rightLegs[unionIndex]));
    }
    for (size_t i = unionIndex; i < leftLegs.size(); i++) {
        m_legs.push_back(leftLegs[i]);
        m_pairedLegs.push_back(std::make_pair(leftLegs[i], std::vector<int>()));
    }
    for (size_t i = unionIndex; i < rightLegs.size(); i++) {
        m_legs.push_back(rightLegs[i]);
        m_pairedLegs.push_back(std::make_pair(std::vector<int>(), rightLegs[i]));
    }
}

void JointNodeTree::sortLegs(std::vector<std::vector<int>> &legs)
{
    const auto &that = this;
    std::sort(legs.begin(), legs.end(), [&that](const std::vector<int> &a, const std::vector<int> &b) -> bool {
        const auto &firstLegEnd = that->joints()[a[a.size() - 1]];
        const auto &secondLegEnd = that->joints()[b[b.size() - 1]];
        return firstLegEnd.position.z() > secondLegEnd.position.z();
    });
}

void JointNodeTree::sortSpine(std::vector<int> &spine)
{
    const auto &that = this;
    std::sort(spine.begin(), spine.end(), [&that](const int &a, const int &b) -> bool {
        const auto &firstNode = that->joints()[a];
        const auto &secondNode = that->joints()[b];
        if (that->isVerticalSpine)
            return firstNode.position.y() > secondNode.position.y();
        else
            return firstNode.position.z() > secondNode.position.z();
    });
}

void JointNodeTree::traceBoneFromJoint(MeshResultContext &resultContext, std::pair<int, int> node, std::set<std::pair<int, int>> &visitedNodes, std::set<std::pair<std::pair<int, int>, std::pair<int, int>>> &connections, int parentIndex)
{
    if (visitedNodes.find(node) != visitedNodes.end())
        return;
    visitedNodes.insert(node);
    const auto &neighbors = resultContext.nodeNeighbors().find(node);
    if (neighbors == resultContext.nodeNeighbors().end())
        return;
    std::vector<std::pair<int, std::pair<int, int>>> neighborJoints;
    for (const auto &it: neighbors->second) {
        if (connections.find(std::make_pair(std::make_pair(node.first, node.second), std::make_pair(it.first, it.second))) != connections.end())
            continue;
        connections.insert(std::make_pair(std::make_pair(node.first, node.second), std::make_pair(it.first, it.second)));
        connections.insert(std::make_pair(std::make_pair(it.first, it.second), std::make_pair(node.first, node.second)));
        const auto &fromNode = resultContext.bmeshNodeMap().find(std::make_pair(node.first, node.second));
        if (fromNode == resultContext.bmeshNodeMap().end()) {
            qDebug() << "bmeshNodeMap find failed:" << node.first << node.second;
            continue;
        }
        const auto &toNode = resultContext.bmeshNodeMap().find(std::make_pair(it.first, it.second));
        if (toNode == resultContext.bmeshNodeMap().end()) {
            qDebug() << "bmeshNodeMap find failed:" << it.first << it.second;
            continue;
        }
        
        JointInfo joint;
        joint.position = toNode->second->origin;
        joint.jointIndex = m_tracedJoints.size();
        joint.partId = toNode->second->bmeshId;
        joint.nodeId = toNode->second->nodeId;
        joint.boneMark = toNode->second->boneMark;
        joint.scale = QVector3D(1.0, 1.0, 1.0);
        joint.radius = toNode->second->radius;
        joint.parentIndex = parentIndex;
        
        m_tracedNodeToJointIndexMap[std::make_pair(it.first, it.second)] = joint.jointIndex;
        
        m_tracedJoints.push_back(joint);
        m_tracedJoints[parentIndex].children.push_back(joint.jointIndex);
        
        neighborJoints.push_back(std::make_pair(joint.jointIndex, it));
    }
    
    for (const auto &joint: neighborJoints) {
        traceBoneFromJoint(resultContext, joint.second, visitedNodes, connections, joint.first);
    }
}

const std::vector<JointInfo> &JointNodeTree::joints() const
{
    return m_tracedJoints;
}

std::vector<JointInfo> &JointNodeTree::joints()
{
    return m_tracedJoints;
}

int JointNodeTree::nodeToJointIndex(int partId, int nodeId) const
{
    const auto &findIt = m_tracedNodeToJointIndexMap.find(std::make_pair(partId, nodeId));
    if (findIt == m_tracedNodeToJointIndexMap.end()) {
        qDebug() << "node to joint index map failed, partId:" << partId << "nodeId:" << nodeId;
        return 0;
    }
    return findIt->second;
}

void JointNodeTree::calculateMatricesByPosition()
{
    if (joints().empty())
        return;
    calculateMatricesByPositionFrom(0, QVector3D(), QVector3D(), QMatrix4x4());
}

void JointNodeTree::calculateMatricesByTransform()
{
    for (auto &joint: joints()) {
        QMatrix4x4 translateMatrix;
        translateMatrix.translate(joint.translation);
        
        QMatrix4x4 rotateMatrix;
        rotateMatrix.rotate(joint.rotation);
        
        QMatrix4x4 scaleMatrix;
        scaleMatrix.scale(joint.scale);
        
        QMatrix4x4 localMatrix = translateMatrix * rotateMatrix * scaleMatrix;
        QMatrix4x4 bindMatrix = joint.parentIndex == -1 ? localMatrix : (joints()[joint.parentIndex].bindMatrix * localMatrix);
        
        bool invertible = true;
        
        joint.bindMatrix = bindMatrix;
        
        joint.position = joint.inverseBindMatrix * joint.bindMatrix * joint.position;
        
        joint.inverseBindMatrix = joint.bindMatrix.inverted(&invertible);
        
        if (!invertible)
            qDebug() << "jointIndex:" << joint.jointIndex << "invertible:" << invertible;
    }
}

void JointNodeTree::calculateMatricesByPositionFrom(int jointIndex, const QVector3D &parentPosition, const QVector3D &parentDirection, const QMatrix4x4 &parentMatrix)
{
    auto &joint = joints()[jointIndex];
    QVector3D translation = joint.position - parentPosition;
    QVector3D direction = QVector3D();
    
    QMatrix4x4 translateMatrix;
    translateMatrix.translate(translation);
    
    QMatrix4x4 rotateMatrix;
    QQuaternion rotation;
    if (!parentDirection.isNull()) {
        direction = translation.normalized();
        
        rotation = QQuaternion::rotationTo(parentDirection, direction);
        rotateMatrix.rotate(rotation);
    }
    
    QMatrix4x4 scaleMatrix;
    scaleMatrix.scale(joint.scale);
    
    QMatrix4x4 localMatrix = translateMatrix * rotateMatrix * scaleMatrix;
    QMatrix4x4 bindMatrix = parentMatrix * localMatrix;
    
    bool invertible = true;
    
    joint.localMatrix = localMatrix;
    joint.translation = translation;
    joint.rotation = rotation;
    joint.bindMatrix = bindMatrix;
    joint.inverseBindMatrix = joint.bindMatrix.inverted(&invertible);
    
    if (!invertible)
        qDebug() << "jointIndex:" << jointIndex << "invertible:" << invertible;
    
    for (const auto &child: joint.children) {
        calculateMatricesByPositionFrom(child, joint.position, direction, bindMatrix);
    }
}

void JointNodeTree::recalculateMatricesAfterPositionUpdated()
{
    calculateMatricesByPosition();
}

void JointNodeTree::recalculateMatricesAfterTransformUpdated()
{
    calculateMatricesByTransform();
}

void JointNodeTree::diff(const JointNodeTree &another, RigFrame &rigFrame)
{
    if (rigFrame.translations.size() != joints().size())
        rigFrame = RigFrame(joints().size());
    for (const auto &first: joints()) {
        const auto &second = another.joints()[first.jointIndex];
        if (!qFuzzyCompare(first.translation, second.translation)) {
            rigFrame.updateTranslation(first.jointIndex, second.translation);
        }
        if (!qFuzzyCompare(first.scale, second.scale)) {
            rigFrame.updateScale(first.jointIndex, second.scale);
        }
        if (!qFuzzyCompare(first.rotation, second.rotation)) {
            rigFrame.updateRotation(first.jointIndex, second.rotation);
        }
    }
}

int JointNodeTree::findHubJoint(int jointIndex, std::vector<int> *tracePath) const
{
    int loopJointIndex = joints()[jointIndex].parentIndex;
    while (-1 != loopJointIndex) {
        if (nullptr != tracePath)
            tracePath->push_back(loopJointIndex);
        const auto &joint = joints()[loopJointIndex];
        if (joint.children.size() > 1 || -1 == joint.parentIndex)
            return joint.jointIndex;
        loopJointIndex = joint.parentIndex;
    }
    return -1;
}

int JointNodeTree::findParent(int jointIndex, int parentIndex, std::vector<int> *tracePath) const
{
    int loopJointIndex = joints()[jointIndex].parentIndex;
    while (-1 != loopJointIndex) {
        if (nullptr != tracePath)
            tracePath->push_back(loopJointIndex);
        const auto &joint = joints()[loopJointIndex];
        if (joint.jointIndex == parentIndex)
            return joint.jointIndex;
        loopJointIndex = joint.parentIndex;
    }
    return -1;
}

int JointNodeTree::findSpineFromChild(int jointIndex) const
{
    int loopJointIndex = joints()[jointIndex].parentIndex;
    while (-1 != loopJointIndex) {
        const auto &joint = joints()[loopJointIndex];
        if (joint.boneMark == SkeletonBoneMark::Spine)
            return joint.jointIndex;
        loopJointIndex = joint.parentIndex;
    }
    return -1;
}

void JointNodeTree::collectChildren(int jointIndex, std::vector<int> &children) const
{
    for (const auto &child: joints()[jointIndex].children) {
        children.push_back(child);
    }
    for (const auto &child: joints()[jointIndex].children) {
        collectChildren(child, children);
    }
}

void JointNodeTree::collectTrivialChildren(int jointIndex, std::vector<int> &children) const
{
    for (const auto &child: joints()[jointIndex].children) {
        if (joints()[child].boneMark != SkeletonBoneMark::None) {
            continue;
        }
        std::vector<int> subChildren;
        bool hasNoTrivialNode = false;
        collectTrivialChildrenStopEarlyOnNoTrivial(child, subChildren, hasNoTrivialNode);
        if (hasNoTrivialNode)
            continue;
        children.push_back(child);
        children.insert(children.end(), subChildren.begin(), subChildren.end());
    }
}

void JointNodeTree::collectTrivialChildrenStopEarlyOnNoTrivial(int jointIndex, std::vector<int> &children, bool &hasNoTrivialNode) const
{
    for (const auto &child: joints()[jointIndex].children) {
        if (joints()[child].boneMark != SkeletonBoneMark::None) {
            hasNoTrivialNode = true;
            return;
        }
        children.push_back(child);
        collectTrivialChildrenStopEarlyOnNoTrivial(child, children, hasNoTrivialNode);
    }
}

