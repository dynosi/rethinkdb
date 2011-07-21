#ifndef __SERVER_NESTED_DEMO_REDIS_LIST_VALUES_HPP__
#define	__SERVER_NESTED_DEMO_REDIS_LIST_VALUES_HPP__

#include <string>
#include "server/nested_demo/redis_utils.hpp"
#include "btree/operations.hpp"

/*
 TODO! All this is basically BS, because redis supports inserting and removing
 elements from arbitrary positions, which has to trigger and index shift for
 all the elements right of it.

 The solution is as follows:
 We store a signed index offset in the super value. Whenever an LPUSH is done,
 we decrement the index to be used and increment the index offset. For an RPUSH,
 we increment the index to be used (in fact by adding offset and list_length).
 LINSERT and LREM, which insert/remove elements at a random position, adapt not
 only the offset but also re-insert all elements on their way.

 The weird thing about this is that inserting and removing elements from the
 middle of the list triggers a rewrite of huge parts of it. That might give quite
 unexpected performance characteristics.
 TODO! How much work would it actually be to implement an allocator on a blob?
 */

// TODO! demo super value (contains block_id_t plus size of list) with redis interface methods
struct redis_demo_list_value_t {
    block_id_t nested_root;
    uint32_t list_length;
    int32_t index_offset; // TODO! Explain

public:
    int inline_size(UNUSED block_size_t bs) const {
        return sizeof(nested_root) + sizeof(list_length) + sizeof(index_offset);
    }

    int64_t value_size() const {
        return 0;
    }

    const char *value_ref() const { return NULL; }
    char *value_ref() { return NULL; }
    

    /* Some operations that you can do on a list (resembling redis commands)... */
    
    std::string lindex(value_sizer_t<redis_demo_list_value_t> *super_sizer, boost::scoped_ptr<transaction_t> &transaction, int index) const;

private:
    // TODO! Document
    int translate_index(int index) const {
        if (index >= 0) {
            // We access the indexth element from the start of the list. The list starts
            // at index_offset.
            index += index_offset;
        } else {
            // We access the indexth element from the end of the list. The list ends
            // at list_length + index_offset.
            index += list_length + index_offset;
        }
        // TODO: Handle properly
        guarantee(index >= index_offset && index < (int32_t)list_length + index_offset, "index out of list range");
        return index;
    }
};
template <>
class value_sizer_t<redis_demo_list_value_t> {
public:
    value_sizer_t<redis_demo_list_value_t>(block_size_t bs) : block_size_(bs) { }

    int size(const redis_demo_list_value_t *value) const {
        return value->inline_size(block_size_);
    }

    bool fits(UNUSED const redis_demo_list_value_t *value, UNUSED int length_available) const {
        // It's of constant size...
        return true;
    }

    int max_possible_size() const {
        // It's of constant size...
        return sizeof(redis_demo_list_value_t);
    }

    block_magic_t btree_leaf_magic() const {
        block_magic_t magic = { { 'l', 'r', 'l', 'i' } };
        return magic;
    }

    block_size_t block_size() const { return block_size_; }

protected:
    // The block size.  It's convenient for leaf node code and for
    // some subclasses, too.
    block_size_t block_size_;
};


/* TODO! Implementations...*/
std::string redis_demo_list_value_t::lindex(value_sizer_t<redis_demo_list_value_t> *super_sizer, boost::scoped_ptr<transaction_t> &transaction, int index) const {
    index = translate_index(index);

    boost::scoped_ptr<superblock_t> nested_btree_sb(new virtual_superblock_t());
    nested_btree_sb->set_root_block_id(nested_root);

    // Construct a sizer for the sub tree, using the same block size as the super tree
    value_sizer_t<redis_nested_string_value_t> sizer(super_sizer->block_size());

    got_superblock_t got_superblock;
    got_superblock.sb.swap(nested_btree_sb);
    got_superblock.txn.swap(transaction);
    keyvalue_location_t<redis_nested_string_value_t> kv_location;

    // Construct the key from index
    char key_buf[offsetof(btree_key_t, contents) + LEX_INT_SIZE];
    btree_key_t *key = reinterpret_cast<btree_key_t*>(key_buf);
    key->size = LEX_INT_SIZE;
    to_lex_int(index, key->contents);
    find_keyvalue_location_for_read(&sizer, &got_superblock, key, &kv_location);

    // Get out the string value
    std::string value;
    value.assign(kv_location.value->contents, kv_location.value->length);

    // Swap the transaction back in, we don't need it anymore...
    transaction.swap(kv_location.txn);

    return value;
}

#endif	/* __SERVER_NESTED_DEMO_REDIS_LIST_VALUES_HPP__ */

