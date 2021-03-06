// Copyright (c) Microsoft Open Technologies, Inc. All rights reserved. See License.txt in the project root for license information.

#pragma once

#if !defined(RXCPP_OPERATORS_RX_BUFFER_WITH_TIME_HPP)
#define RXCPP_OPERATORS_RX_BUFFER_WITH_TIME_HPP

#include "../rx-includes.hpp"

namespace rxcpp {

namespace operators {

namespace detail {

template<class T, class Duration, class Coordination>
struct buffer_with_time
{
    static_assert(std::is_convertible<Duration, rxsc::scheduler::clock_type::duration>::value, "Duration parameter must convert to rxsc::scheduler::clock_type::duration");
    static_assert(is_coordination<Coordination>::value, "Coordination parameter must satisfy the requirements for a Coordination");

    typedef rxu::decay_t<T> source_value_type;
    typedef rxu::decay_t<Coordination> coordination_type;
    typedef typename coordination_type::coordinator_type coordinator_type;
    typedef rxu::decay_t<Duration> duration_type;

    struct buffer_with_time_values
    {
        buffer_with_time_values(duration_type p, duration_type s, coordination_type c)
            : period(p)
            , skip(s)
            , coordination(c)
        {
        }
        duration_type period;
        duration_type skip;
        coordination_type coordination;
    };
    buffer_with_time_values initial;

    buffer_with_time(duration_type period, duration_type skip, coordination_type coordination)
        : initial(period, skip, coordination)
    {
    }

    template<class Subscriber>
    struct buffer_with_time_observer
    {
        typedef buffer_with_time_observer<Subscriber> this_type;
        typedef std::vector<T> value_type;
        typedef rxu::decay_t<Subscriber> dest_type;
        typedef observer<value_type, this_type> observer_type;

        struct buffer_with_time_subscriber_values : public buffer_with_time_values
        {
            buffer_with_time_subscriber_values(composite_subscription cs_, dest_type d_, buffer_with_time_values v_, coordinator_type c_)
                : buffer_with_time_values(v_)
                , cs(std::move(cs_))
                , dest(std::move(d_))
                , coordinator(std::move(c_))
                , worker(coordinator.get_worker())
                , expected(worker.now())
            {
            }
            composite_subscription cs;
            dest_type dest;
            coordinator_type coordinator;
            rxsc::worker worker;
            mutable std::deque<value_type> chunks;
            rxsc::scheduler::clock_type::time_point expected;
        };
        std::shared_ptr<buffer_with_time_subscriber_values> state;

        buffer_with_time_observer(composite_subscription cs, dest_type d, buffer_with_time_values v, coordinator_type c)
            : state(std::make_shared<buffer_with_time_subscriber_values>(buffer_with_time_subscriber_values(std::move(cs), std::move(d), v, std::move(c))))
        {
            auto localState = state;

            auto disposer = [=](const rxsc::schedulable&){
                localState->cs.unsubscribe();
                localState->dest.unsubscribe();
                localState->worker.unsubscribe();
            };
            auto selectedDisposer = on_exception(
                [&](){return localState->coordinator.act(disposer);},
                localState->dest);
            if (selectedDisposer.empty()) {
                return;
            }

            localState->dest.add([=](){
                localState->worker.schedule(selectedDisposer.get());
            });
            localState->cs.add([=](){
                localState->worker.schedule(selectedDisposer.get());
            });

            //
            // The scheduler is FIFO for any time T. Since the observer is scheduling
            // on_next/on_error/oncompleted the timed schedule calls must be resheduled
            // when they occur to ensure that production happens after on_next/on_error/oncompleted
            //

            auto produce_buffer = [localState](const rxsc::schedulable&) {
                localState->dest.on_next(std::move(localState->chunks.front()));
                localState->chunks.pop_front();
            };
            auto selectedProduce = on_exception(
                [&](){return localState->coordinator.act(produce_buffer);},
                localState->dest);
            if (selectedProduce.empty()) {
                return;
            }

            auto create_buffer = [localState, selectedProduce](const rxsc::schedulable&) {
                localState->chunks.emplace_back();
                auto produce_at = localState->expected + localState->period;
                localState->expected += localState->skip;
                localState->worker.schedule(produce_at, [localState, selectedProduce](const rxsc::schedulable&) {
                    localState->worker.schedule(selectedProduce.get());
                });
            };
            auto selectedCreate = on_exception(
                [&](){return localState->coordinator.act(create_buffer);},
                localState->dest);
            if (selectedCreate.empty()) {
                return;
            }

            state->worker.schedule_periodically(
                state->expected,
                state->skip,
                [localState, selectedCreate](const rxsc::schedulable&) {
                    localState->worker.schedule(selectedCreate.get());
                });
        }
        void on_next(T v) const {
            auto localState = state;
            auto work = [v, localState](const rxsc::schedulable&){
                for(auto& chunk : localState->chunks) {
                    chunk.push_back(v);
                }
            };
            auto selectedWork = on_exception(
                [&](){return localState->coordinator.act(work);},
                localState->dest);
            if (selectedWork.empty()) {
                return;
            }
            localState->worker.schedule(selectedWork.get());
        }
        void on_error(std::exception_ptr e) const {
            auto localState = state;
            auto work = [e, localState](const rxsc::schedulable&){
                localState->dest.on_error(e);
            };
            auto selectedWork = on_exception(
                [&](){return localState->coordinator.act(work);},
                localState->dest);
            if (selectedWork.empty()) {
                return;
            }
            localState->worker.schedule(selectedWork.get());
        }
        void on_completed() const {
            auto localState = state;
            auto work = [localState](const rxsc::schedulable&){
                on_exception(
                    [&](){
                        while (!localState->chunks.empty()) {
                            localState->dest.on_next(std::move(localState->chunks.front()));
                            localState->chunks.pop_front();
                        }
                        return true;
                    },
                    localState->dest);
                localState->dest.on_completed();
            };
            auto selectedWork = on_exception(
                [&](){return localState->coordinator.act(work);},
                localState->dest);
            if (selectedWork.empty()) {
                return;
            }
            localState->worker.schedule(selectedWork.get());
        }

        static subscriber<T, observer<T, this_type>> make(dest_type d, buffer_with_time_values v) {
            auto cs = composite_subscription();
            auto coordinator = v.coordination.create_coordinator();

            return make_subscriber<T>(cs, this_type(cs, std::move(d), std::move(v), std::move(coordinator)));
        }
    };

    template<class Subscriber>
    auto operator()(Subscriber dest) const
        -> decltype(buffer_with_time_observer<Subscriber>::make(std::move(dest), initial)) {
        return      buffer_with_time_observer<Subscriber>::make(std::move(dest), initial);
    }
};

template<class Duration, class Coordination>
class buffer_with_time_factory
{
    typedef rxu::decay_t<Duration> duration_type;
    typedef rxu::decay_t<Coordination> coordination_type;

    duration_type period;
    duration_type skip;
    coordination_type coordination;
public:
    buffer_with_time_factory(duration_type p, duration_type s, coordination_type c) : period(p), skip(s), coordination(c) {}
    template<class Observable>
    auto operator()(Observable&& source)
        -> decltype(source.template lift<std::vector<rxu::value_type_t<rxu::decay_t<Observable>>>>(buffer_with_time<rxu::value_type_t<rxu::decay_t<Observable>>, Duration, Coordination>(period, skip, coordination))) {
        return      source.template lift<std::vector<rxu::value_type_t<rxu::decay_t<Observable>>>>(buffer_with_time<rxu::value_type_t<rxu::decay_t<Observable>>, Duration, Coordination>(period, skip, coordination));
    }
};

}

template<class Duration, class Coordination>
inline auto buffer_with_time(Duration period, Coordination coordination)
    ->      detail::buffer_with_time_factory<Duration, Coordination> {
    return  detail::buffer_with_time_factory<Duration, Coordination>(period, period, coordination);
}

template<class Duration, class Coordination>
inline auto buffer_with_time(Duration period, Duration skip, Coordination coordination)
    ->      detail::buffer_with_time_factory<Duration, Coordination> {
    return  detail::buffer_with_time_factory<Duration, Coordination>(period, skip, coordination);
}

}

}

#endif
