#include <iostream>
#include <string>

#include <couchbase/client/cluster.hxx>
#include <couchbase/transactions.hxx>

struct Player {
    int experience;
    int hitpoints;
    std::string json_type;
    int level;
    bool logged_in;
    std::string name;
    std::string uuid;
};

void
to_json(nlohmann::json& j, const Player& p)
{
    /* clang-format off */
    j = nlohmann::json{
        { "experience", p.experience },
        { "hitpoints", p.hitpoints },
        { "jsonType", p.json_type },
        { "level", p.level },
        { "loggedIn", p.logged_in },
        { "name", p.name },
        { "uuid", p.uuid },
    };
    /* clang-format on */
}

void
from_json(const nlohmann::json& j, Player& p)
{
    j.at("experience").get_to(p.experience);
    j.at("hitpoints").get_to(p.hitpoints);
    j.at("jsonType").get_to(p.json_type);
    j.at("level").get_to(p.level);
    j.at("loggedIn").get_to(p.logged_in);
    j.at("name").get_to(p.name);
    j.at("uuid").get_to(p.uuid);
}

struct Monster {
    int experience_when_killed;
    int hitpoints;
    double item_probability;
    std::string json_type;
    std::string name;
    std::string uuid;
};

void
to_json(nlohmann::json& j, const Monster& m)
{
    j = nlohmann::json{
        { "experienceWhenKilled", m.experience_when_killed },
        { "hitpoints", m.hitpoints },
        { "itemProbability", m.item_probability },
        { "jsonType", m.json_type },
        { "name", m.name },
        { "uuid", m.uuid },
    };
}

void
from_json(const nlohmann::json& j, Monster& m)
{
    j.at("experienceWhenKilled").get_to(m.experience_when_killed);
    j.at("hitpoints").get_to(m.hitpoints);
    j.at("itemProbability").get_to(m.item_probability);
    j.at("jsonType").get_to(m.json_type);
    j.at("name").get_to(m.name);
    j.at("uuid").get_to(m.uuid);
}

int
calculate_level_for_experience(int experience) const
{
    return experience / 100;
}

void
player_hits_monster(couchbase::transactions::transactions& transactions,
                    std::shared_ptr<couchbase::collection> collection,
                    const std::string& action_id,
                    int damage,
                    const std::string& player_id,
                    const std::string& monster_id)
{
    // tag::full[]
    try {
        transactions.run([&](couchbase::transactions::attempt_context& ctx) {
            auto monster = ctx.get(collection, monster_id);
            const Monster& monster_body = monster.content<Monster>();

            int monster_hitpoints = monster_body.hitpoints;
            int monster_new_hitpoints = monster_hitpoints - damage;

            auto player = ctx.get(collection, player_id);

            if (monster_new_hitpoints <= 0) {
                // Monster is killed. The remove is just for demoing, and a more realistic examples would set a "dead" flag or similar.
                ctx.remove(collection, monster);

                const Player& player_body = player.content<Player>();

                // the player earns experience for killing the monster
                int experience_for_killing_monster = monster_body.experience_when_killed;
                int player_experience = player_body.experience;
                int player_new_experience = player_experience + experience_for_killing_monster;
                int player_new_level = calculate_level_for_experience(player_new_experience);

                Player player_new_body = player_body;
                player_new_body.experience = player_new_experience;
                player_new_body.level = player_new_level;
                ctx.replace(collection, player, player_new_body);
            } else {
                Monster monster_new_body = monster_body;
                monster_new_body.hitpoints = monster_new_hitpoints;
                ctx.replace(collection, monster, monster_new_body);
            }
        });
    } catch (couchbase::transactions::transaction_failed& e) {
        // The operation failed. Both the monster and the player will be untouched

        // Situations that can cause this would include either the monster
        // or player not existing (as get is used), or a persistent
        // failure to be able to commit the transaction, for example on
        // prolonged node failure.
    }
    // end::full[]
}

int
main(int argc, const char* argv[])
{
    // #tag::init[]
    // Initialize the Couchbase cluster
    couchbase::cluster cluster("couchbase://localhost", "transactor", "mypass");
    auto bucket = cluster.bucket("transact");
    auto collection = bucket->default_collection();

    // Create the single Transactions object
    couchbase::transactions::transactions transactions(cluster, {});
    // #end::init[]

    {
        // #tag::config[]
        couchbase::transactions::transaction_config configuration;
        configuration.durability_level(couchbase::transactions::durability_level::PERSIST_TO_MAJORITY);
        couchbase::transactions::transactions transactions(cluster, configuration);
        // #end::config[]
    }

    {
        // #tag::config-expiration[]
        couchbase::transactions::transaction_config configuration;
        configuration.expiration_time(std::chrono::seconds(120));
        couchbase::transactions::transactions transactions(cluster, configuration);
        // #end::config-expiration[]
    }

    {
        // #tag::config-cleanup[]
        couchbase::transactions::transaction_config configuration;
        configuration.cleanup_client_attempts(false);
        configuration.cleanup_lost_attempts(false);
        configuration.cleanup_window(std::chrono::seconds(120));
        couchbase::transactions::transactions transactions(cluster, configuration);
        // #end::config-cleanup[]
    }

    {
        // #tag::create[]
        try {
            transactions.run([&](couchbase::transactions::attempt_context& ctx) {
                // 'ctx' permits getting, inserting, removing and replacing documents,
                // along with committing and rolling back the transaction

                // ... Your transaction logic here ...

                // This call is optional -- if you leave it off,
                // the transaction will be committed anyway.
                ctx.commit();
            });
        } catch (couchbase::transactions::transaction_failed& e) {
            std::cerr << "Transaction did not reach commit point: " << e.what() << "\n";
        }
        // #end::create[]
    }

    {
        // #tag::examples[]
        try {
            transactions.run([&](couchbase::transactions::attempt_context& ctx) {
                // Inserting a doc:
                ctx.insert(collection, "doc-a", nlohmann::json({}));

                // Getting documents:
                // Use ctx.get_optional() if the document may or may not exist
                auto doc_opt = ctx.get_optional(collection, "doc-a");
                if (doc_opt) {
                    couchbase::transactions::transaction_document& doc = doc_opt.value();
                }

                // Use ctx.get if the document should exist, and the transaction
                // will fail if it does not
                couchbase::transactions::transaction_document doc_a = ctx.get(collection, "doc-a");

                // Replacing a doc:
                couchbase::transactions::transaction_document doc_b = ctx.get(collection, "doc-b");
                nlohmann::json content = doc_b.content<nlohmann::json>();
                content["transactions"] = "are awesome";
                ctx.replace(collection, doc_b, content);

                // Removing a doc:
                couchbase::transactions::transaction_document doc_c = ctx.get(collection, "doc-c");
                ctx.remove(collection, doc_c);

                ctx.commit();
            });
        } catch (couchbase::transactions::transaction_failed& e) {
            std::cerr << "Transaction did not reach commit point: " << e.what() << "\n";
        }
        // #end::examples[]
    }

    {
        // tag::insert[]
        transactions.run([&](couchbase::transactions::attempt_context& ctx) {
            std::string id = "doc_id";
            nlohmann::json value{
                { "foo", "bar" },
            };
            ctx.insert(collection, id, value);
        });
        // end::insert[]
    }

    {
        // #tag::get[]
        transactions.run([&](couchbase::transactions::attempt_context& ctx) {
            std::string id = "doc-a";
            auto doc_opt = ctx.get_optional(collection, id);
            if (doc_opt) {
                couchbase::transactions::transaction_document& doc = doc_opt.value();
            }
        });
        // #end::get[]
    }

    {
        // #tag::getReadOwnWrites[]
        transactions.run([&](couchbase::transactions::attempt_context& ctx) {
            std::string id = "doc_id";
            nlohmann::json value{
                { "foo", "bar" },
            };
            ctx.insert(collection, id, value);
            // document must be accessible
            couchbase::transactions::transaction_document doc = ctx.get(collection, id);
        });
        // #end::getReadOwnWrites[]
    }

    {
        // tag::replace[]
        transactions.run([&](couchbase::transactions::attempt_context& ctx) {
            std::string id = "doc-a";
            couchbase::transactions::transaction_document doc = ctx.get(collection, id);
            nlohmann::json content = doc.content<nlohmann::json>();
            content["transactions"] = "are awesome";
            ctx.replace(collection, doc, content);
        });
        // end::replace[]
    }

    {
        // #tag::remove[]
        transactions.run([&](couchbase::transactions::attempt_context& ctx) {
            std::string id = "doc-a";
            auto doc_opt = ctx.get_optional(collection, id);
            if (doc_opt) {
                ctx.remove(collection, doc_opt.value());
            }
        });
        // #end::remove[]
    }

    {
        int cost_of_item = 10;
        // #tag::rollback[]
        transactions.run([&](couchbase::transactions::attempt_context& ctx) {
            couchbase::transactions::transaction_document customer = ctx.get(collection, "customer-name");

            auto content = customer.content<nlohmann::json>();
            int balance = content["balance"].get<int>();
            if (balance < cost_of_item) {
                ctx.rollback();
            }
            // else continue transaction
        });
        // #end::rollback[]
    }
}
