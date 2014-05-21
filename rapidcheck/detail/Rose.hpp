#pragma once

#include <sstream>

#include "rapidcheck/Show.hpp"

#include "ImplicitParam.hpp"
#include "RandomEngine.hpp"

namespace rc {
namespace detail {

//! Represents the structure of value generation where large complex values are
//! generated from small simple values. This also means that large values often
//! can be shrunk by shrinking the small values individually.
class RoseNode
{
public:
    //! Constructs a new root \c RoseNode.
    RoseNode() : RoseNode(nullptr) {}

    //! Returns an atom. If one has already been generated, it's reused. If not,
    //! a new one is generated.
    RandomEngine::Atom atom()
    {
        if (!m_hasAtom) {
            ImplicitParam<param::RandomEngine> randomEngine;
            m_atom = randomEngine->nextAtom();
            m_hasAtom = true;
        }

        return m_atom;
    }

    //! Outputs a string representation of this node and all its children.
    void print(std::ostream &os)
    {
        for (int i = 0; i < depth(); i++)
            os << "  ";
        os << "- " << description() << std::endl;

        for (auto &child : m_children)
            child.print(os);
    }

    //! Generates a value in this node using the given generator.
    template<typename Gen>
    typename Gen::GeneratedType generate(const Gen &generator)
    {
        typedef typename Gen::GeneratedType T;
        ImplicitParam<ShrunkNode> shrunkNode;

        m_lastGenerator = UntypedGeneratorUP(new Gen(generator));

        if (shrunkNode.hasBinding() && (*shrunkNode == nullptr)) {
            if (!m_shrinkIterator) {
                T value(regenerate<T>());
                // Did children shrink before us?
                if (*shrunkNode != nullptr)
                    return value;

                m_shrinkIterator = generator.shrink(value);
                // We need a fallback accepted generator if shrinking fails
                if (!m_acceptedGenerator)
                    m_acceptedGenerator = UntypedGeneratorUP(new Gen(generator));
            }

            if (m_shrinkIterator->hasNext()) {
                auto typedIterator =
                    dynamic_cast<ShrinkIterator<T> *>(m_shrinkIterator.get());
                assert(typedIterator != nullptr);
                m_shrunkGenerator = UntypedGeneratorUP(
                    new Constant<T>(typedIterator->next()));
                *shrunkNode = this;
            } else {
                // Shrinking exhausted
                m_shrunkGenerator = nullptr;
            }
        }

        return regenerate<T>();
    }

    //! Picks a value using the given generator in the context of the current
    //! node.
    template<typename Gen>
    typename Gen::GeneratedType pick(const Gen &generator)
    {
        ImplicitParam<NextChildIndex> nextChildIndex;
        if (*nextChildIndex >= m_children.size())
            m_children.push_back(RoseNode(this));
        (*nextChildIndex)++;
        return m_children[*nextChildIndex - 1].generate(generator);
    }

    //! Returns a list of \c ValueDescriptions from the immediate children of
    //! this node.
    std::vector<std::string> example()
    {
        std::vector<std::string> values;
        values.reserve(m_children.size());
        for (auto &child : m_children)
            values.push_back(child.stringValue());
        return values;
    }

    //! Returns a string representation of the value of this node or an empty
    //! if one hasn't been decided.
    std::string stringValue()
    {
        ImplicitParam<CurrentNode> currentNode;
        currentNode.let(this);
        ImplicitParam<NextChildIndex> nextChildIndex;
        nextChildIndex.let(0);

        UntypedGenerator *generator = activeGenerator();
        if (generator != nullptr)
            return generator->generateString();
        else
            return std::string();
    }

    //! Tries to find an immediate shrink that yields \c false for the given
    //! generator.
    //!
    //! @return  A tuple where the first value tells whether the shrinking was
    //!          successful and the second how many shrinks were tried,
    //!          regardless of success.
    template<typename Gen>
    std::tuple<bool, int> shrink(const Gen &generator)
    {
        ImplicitParam<ShrunkNode> shrunkNode;
        int numTries = 0;
        bool result = true;
        while (result) {
            numTries++;
            shrunkNode.let(nullptr);
            result = generate(generator);
            if (*shrunkNode == nullptr)
                return std::make_tuple(false, numTries);
        }

        (*shrunkNode)->acceptShrink();
        return std::make_tuple(true, numTries);
    }

    //! Move constructor.
    RoseNode(RoseNode &&other)
        : m_parent(other.m_parent)
        , m_children(std::move(other.m_children))
        , m_hasAtom(other.m_hasAtom)
        , m_atom(other.m_atom)
        , m_lastGenerator(std::move(other.m_lastGenerator))
        , m_acceptedGenerator(std::move(other.m_acceptedGenerator))
        , m_shrunkGenerator(std::move(other.m_shrunkGenerator))
        , m_shrinkIterator(std::move(other.m_shrinkIterator))
    {
        adoptChildren();
    }

    //! Move assignment
    RoseNode &operator=(RoseNode &&rhs)
    {
        m_parent = rhs.m_parent;
        m_children = std::move(rhs.m_children);
        m_hasAtom = rhs.m_hasAtom;
        m_atom = rhs.m_atom;
        m_lastGenerator = std::move(rhs.m_lastGenerator);
        m_acceptedGenerator = std::move(rhs.m_acceptedGenerator);
        m_shrunkGenerator = std::move(rhs.m_shrunkGenerator);
        m_shrinkIterator = std::move(rhs.m_shrinkIterator);
        adoptChildren();
        return *this;
    }

    void printExample()
    {
        for (const auto &desc : example())
            std::cout << desc << std::endl;
    }

    //! Returns a reference to the current node.
    static RoseNode &current()
    { return **ImplicitParam<CurrentNode>(); }

private:
    RC_DISABLE_COPY(RoseNode)

    // Implicit parameters, see ImplicitParam
    struct CurrentNode { typedef RoseNode *ValueType; };
    struct NextChildIndex { typedef size_t ValueType; };
    struct ShrunkNode { typedef RoseNode *ValueType; };

    //! Constructs a new \c RoseNode with the given parent or \c 0 if it should
    //! have no parent, i.e. is root.
    explicit RoseNode(RoseNode *parent) : m_parent(parent) {}

    //! Returns the depth of this node.
    int depth() const
    {
        if (m_parent == nullptr)
            return 0;

        return m_parent->depth() + 1;
    }

    //! Sets the parent of all children to this node.
    void adoptChildren()
    {
        for (auto &child : m_children)
            child.m_parent = this;
    }

    //! Returns a description of this node.
    std::string description() const
    {
        return generatorName();
    }

    //! Returns the index of this node among its sibilings. Returns \c -1 if
    //! node is root.
    std::ptrdiff_t index() const
    {
        if (m_parent == nullptr)
            return -1;

        auto &siblings = m_parent->m_children;
        auto it = std::find_if(
            siblings.begin(),
            siblings.end(),
            [this](const RoseNode &node){ return &node == this; });

        return it - siblings.begin();
    }

    //! Returns a string describing the path to this node from the root node.
    std::string path()
    {
        if (m_parent == nullptr)
            return "/ " + description();
        else
            return m_parent->path() + " / " + description();
    }

    //! Returns the active generator.
    UntypedGenerator *activeGenerator() const
    {
        if (m_shrunkGenerator)
            return m_shrunkGenerator.get();
        else if (m_acceptedGenerator)
            return m_acceptedGenerator.get();
        else if (m_lastGenerator)
            return m_lastGenerator.get();
        else
            return nullptr;
    }

    //! Returns the name of the active generator
    std::string generatorName() const
    {
        auto gen = activeGenerator();
        if (gen == nullptr)
            return std::string();
        else
            return demangle(typeid(*gen).name());
    }

    //! Returns the active generator cast to a generator of the given type or
    //! \c default if there is none or if there is a type mismatch.
    template<typename T>
    T regenerate()
    {
        ImplicitParam<CurrentNode> currentNode;
        currentNode.let(this);
        ImplicitParam<NextChildIndex> nextChildIndex;
        nextChildIndex.let(0);
        return (*dynamic_cast<Generator<T> *>(activeGenerator()))();
    }

    //! Accepts the current shrink value
    void acceptShrink()
    {
        if (!m_shrunkGenerator)
            return;
        m_acceptedGenerator = std::move(m_shrunkGenerator);
        m_shrinkIterator = nullptr;
    }

    typedef std::vector<RoseNode> Children;
    RoseNode *m_parent;
    Children m_children;
    bool m_hasAtom = false;
    RandomEngine::Atom m_atom;
    UntypedGeneratorUP m_lastGenerator;
    UntypedGeneratorUP m_acceptedGenerator;
    UntypedGeneratorUP m_shrunkGenerator;
    UntypedShrinkIteratorUP m_shrinkIterator;
};

} // namespace detail
} // namespace rc