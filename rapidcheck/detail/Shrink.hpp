#pragma once

namespace rc {

template<typename T>
class NullIterator : public ShrinkIterator<T>
{
public:
    bool hasNext() const override { return false; }
    T next() { for (;;); } // Should never happen
};

template<typename T>
class DivideByTwoIterator : public ShrinkIterator<T>
{
public:
    DivideByTwoIterator(T value) : m_currentValue(value) {}

    bool hasNext() const override { return m_currentValue != 0; }

    T next() override
    {
        m_currentValue /= 2;
        return m_currentValue;
    }

private:
    T m_currentValue;
};

template<typename T>
class RemoveElementIterator : public ShrinkIterator<T>
{
public:
    RemoveElementIterator(T collection)
        : m_collection(std::move(collection))
    { m_skipElement = m_collection.begin(); }

    bool hasNext() const override
    { return m_skipElement != m_collection.end(); }

    T next() override
    {
        T shrunk;
        for (auto it = m_collection.begin(); it != m_collection.end(); it++) {
            if (it != m_skipElement)
                shrunk.insert(shrunk.end(), *it);
        }

        m_skipElement++;
        return shrunk;
    }

private:
    typename T::const_iterator m_skipElement;
    T m_collection;
};

template<typename T,
         typename I,
         typename Predicate,
         typename Iterate>
class UnfoldIterator : public ShrinkIterator<T>
{
public:
    UnfoldIterator(I initial, Predicate predicate, Iterate iterate)
        : m_it(initial), m_predicate(predicate), m_iterate(iterate) {}

    bool hasNext() const override { return m_predicate(m_it); }

    T next() override
    {
        std::pair<T, I> result(m_iterate(m_it));
        m_it = result.second;
        return result.first;
    }

private:
    I m_it;
    Predicate m_predicate;
    Iterate m_iterate;
};

template<typename I,
         typename Predicate,
         typename Iterate>
ShrinkIteratorUP<typename std::result_of<Iterate(I)>::type::first_type>
unfold(I initial, Predicate predicate, Iterate iterate)
{
    typedef typename decltype(iterate(initial))::first_type T;
    return ShrinkIteratorUP<T>(new UnfoldIterator<T, I, Predicate, Iterate>(
                                   initial, predicate, iterate));
}

//! Shrinks collections by trying to shrink each element in turn.
// template<typename T>
// class ShrinkElementIterator
// {
// public:
//     ShrinkElementIterator(T collection, )
//         : m_collection(std::move(collection))
//         , m_shrinkElement(m_collection.begin())
//     {
//         if (!m_collection.empty()) {
//             m_elementShrinker = std::unique_ptr<ElementShrinker>(
//                 new ElementShrinker(
//                     Shrinkable<Element>::shrink(*m_shrinkElement)));
//             advance();
//         }
//     }

//     bool hasNext() const override
//     { return !m_collection.empty() && m_elementShrinker->hasNext(); }

//     T next() override
//     {
//         T shrunk;
//         for (auto it = m_collection.begin(); it != m_collection.end(); it++)
//         {
//             if (it == m_shrinkElement)
//                 shrunk.insert(shrunk.end(), m_elementShrinker->next());
//             else
//                 shrunk.insert(shrunk.end(), *it);
//         }

//         advance();
//         return shrunk;
//     }

// private:
//     void advance()
//     {
//         while (!m_elementShrinker->hasNext() &&
//                (m_shrinkElement != m_collection.end()))
//         {
//             m_shrinkElement++;
//             if (m_shrinkElement == m_collection.end())
//                 break;
//             *m_elementShrinker = Shrinkable<Element>::shrink(*m_shrinkElement);
//         }
//     }

//     T m_collection;
//     typename T::const_iterator m_shrinkElement;
//     // We must use a pointer here since ElementShrinker is probably not default
//     // constructible.
//     std::unique_ptr<ElementShrinker> m_elementShrinker;
// };

} // namespace rc