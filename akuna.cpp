#include <map>
#include <ostream>
#include <set>
#include <list>
#include <cmath>
#include <ctime>
#include <deque>
#include <queue>
#include <stack>
#include <string>
#include <bitset>
#include <cstdio>
#include <limits>
#include <vector>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <functional>

#define TRACE std::cout << __LINE__ << std::endl;
#define DEBUG
#define DEBUG_LINE

typedef enum {
    OPERATION_NONE,
    BUY,
    SELL,
    CANCEL,
    MODIFY,
    PRINT
} operation_t;

typedef enum {
    ORDER_NONE,
    IOC,
    GFD
} order_t;

static const std::unordered_map<const std::string_view, operation_t> operation_map = {
    {"BUY", BUY},
    {"SELL", SELL},
    {"CANCEL", CANCEL},
    {"MODIFY", MODIFY},
    {"PRINT", PRINT}
};

static const std::unordered_map<const std::string_view, order_t> order_type_map = {
    {"IOC", IOC},
    {"GFD", GFD}
};

operation_t create_operation(const std::string_view s) {
    const auto it = operation_map.find(s);
    return it == operation_map.end() ? OPERATION_NONE : it->second;
}

order_t create_order_type(const std::string_view s) {
    const auto it = order_type_map.find(s);
    return it == order_type_map.end() ? ORDER_NONE : it->second;
}

struct order {
    typedef std::shared_ptr<order> pointer_type;

    std::string id;
    operation_t direction; // for more efficient cancel
    int price;
    int qty;

    bool valid() const {
       return !id.empty() && price > 0 && qty > 0;
    }
};

std::ostream& operator<<(std::ostream& ostr, const order::pointer_type o) {
    ostr << (o->direction == BUY ? "BUY" : "SELL") << " " << o->id << " " << o->price << " " << o->qty;
    return ostr;
}

bool buy_cross(int a, int b) {
    return a <= b;
}

bool sell_cross(int a, int b) {
    return a >= b;
}


/*
- match: price cross: lowest & highest price -- What about orders with same price?? use shift to maintain priority (deque?)
- cancel: order id - fetch from all_orders, use direction & price to remove from book
- modify: order id - fetch from all_orders, use direction & price to update book, remember push_back
- print: sort by price - buy/sell already ordered by price
*/
class order_book {
    typedef std::deque<order::pointer_type> queue_type;
    // k = price, v = insert order sorted list of orders at that price.
    typedef std::map<int, queue_type> order_book_type;

    // FIXME:
    typedef bool (*price_cross)(int,int);
    //typedef std::function<bool(int,int)> price_cross;

    order_book_type sell_orders_;
    std::map<int, queue_type, std::greater<int>> buy_orders_;

    // k = order_id, v = the order
    std::unordered_map<std::string, order::pointer_type> all_orders_;

    // sorry about the param names, I don't remember the correct nomenclature atm
    template<typename Book, typename Queue>
    void match(order::pointer_type o,
               order_t t,
               price_cross cross_fn,
               Book& orders,
               Queue& backlog){

        if(!o->valid()) return;

        // special case, if order_id exists ignore
        if (all_orders_.contains(o->id)) return;

        std::vector<order::pointer_type> cleanup;

        auto it = orders.begin(); // it points to lowest selling price

        // iterate over prices
        while(it != orders.end() && o->qty > 0) {
            auto price = (*it).first;
            if(!cross_fn(price, o->price)) {
                break;
            }
            // iterate over the queue
            auto& q = (*it).second;
            auto q_it = q.begin();
            while(q_it != q.end() && o->qty > 0) {
                auto matched = *q_it;
                int trade_qty = std::min(matched->qty, o->qty);

                std::cout << "TRADE"
                        << " " << matched->id
                        << " " << matched->price
                        << " " << trade_qty
                        << " " << o->id
                        << " " << o->price
                        << " " << trade_qty
                        << std::endl;
                o->qty -= trade_qty;
                matched->qty -= trade_qty;

                if(matched->qty == 0) {
                    cleanup.push_back(matched);
                }

                ++q_it;
            }
            ++it;
        }
        if(o->qty > 0) {
            if (t == IOC) {
                for(auto c : cleanup) {
                    // FIXME: might need another method if cancel ever does more than remove the order
                    cancel(c->id);
                }
                return;
            }

            backlog[o->price].push_back(o);
            all_orders_.insert(std::make_pair(o->id, o));
        }

        for(auto c : cleanup) {
            // FIXME: might need another method if cancel ever does more than remove the order
            cancel(c->id);
        }
    }

public:


    void buy(order::pointer_type o, order_t t) {
        match(o, t, buy_cross, sell_orders_, buy_orders_);
    }
    void sell(order::pointer_type o, order_t t) {
        match(o, t, sell_cross, buy_orders_, sell_orders_);
    }

    void cancel(const std::string& order_id) {
        auto it = all_orders_.find(order_id);
        if (it == all_orders_.end())  {
            return;
        }
        auto o = (*it).second;
        auto& book = (o->direction == BUY) ? sell_orders_ : buy_orders_;
        auto& q = book[o->price];
        q.erase(std::remove_if(q.begin(), q.end(),
            [&](auto& ptr) { return ptr->id == order_id; }));
        if (q.size() == 0) book.erase(o->price);
        all_orders_.erase(it);
    }

    void modify(order::pointer_type o) {
        cancel(o->id);
        all_orders_.insert(std::make_pair(o->id, o));
        auto& book = o->direction == BUY ? sell_orders_ : buy_orders_;
        book[o->price].push_back(o);
    }

    template<typename BookIter>
    void print(BookIter bit, BookIter end ) const {
        while(bit != end) {
            int sum = 0;
            auto& q = (*bit).second;
            auto it = q.cbegin();
            while (it != q.cend()) {
                sum += (*it)->qty;
                it++;
            }
            std::cout << (*bit).first << " " << sum << std::endl;
            bit++;
        }
    }

    void print() const {
        // buy & sell lists both output in decreasing order
        std::cout << "SELL:" <<std::endl;
        print(sell_orders_.rbegin(), sell_orders_.rend());
        std::cout << "BUY:" <<std::endl;
        print(buy_orders_.begin(), buy_orders_.end());
    }
};

int main(int argc, char **argv) {
    order_book book;
    std::string line;
    while(std::getline(std::cin, line)) {
        std::istringstream istr(line);
        std::string op_s;
        istr >> op_s;
        if (op_s.empty())
            continue;
        operation_t op = create_operation(op_s);

        switch(op) {
            case BUY:
            case SELL: {
                std::string order_s;
                int price, qty;
                std::string order_id;
                istr >> order_s >> price >> qty >> order_id;
                order_t kind = create_order_type(order_s);
                auto o = std::make_shared<order>(order {
                    .id = std::move(order_id),
                    .direction = op,
                    .price = price,
                    .qty = qty,
                });

                if(op == BUY) {
                    book.buy(o, kind);
                } else {
                    book.sell(o, kind);
                }

            } break;
            case CANCEL: {
                std::string order_id;
                istr >> order_id;
                book.cancel(order_id);
            } break;
            case MODIFY: {
                std::string order_id;
                std::string order_dir_s;
                int price, qty;

                istr >> order_id >> order_dir_s >> price >> qty;
                operation_t dir = create_operation(order_dir_s); // bit of a hack, this argument happens to be a subset of order_t. TODO create another enum?
                auto o = std::make_shared<order>(order {
                    .id = std::move(order_id),
                    .direction = dir,
                    .price = price,
                    .qty = qty,
                });
                book.modify(o);
            } break;
            case PRINT: {
                book.print();
            } break;
            default:
                throw new std::runtime_error("parsed invalid operation");
        }
    }

    return 0;
}
