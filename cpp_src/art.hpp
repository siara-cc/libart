#ifndef ART_H
#define ART_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>

#ifdef __i386__
    #include <emmintrin.h>
#else
#ifdef __amd64__
    #include <emmintrin.h>
#endif
#endif

namespace art {

#define NODE4   1
#define NODE16  2
#define NODE48  3
#define NODE256 4

#define MAX_PREFIX_LEN 10

#define IS_LEAF(x) (((uintptr_t)x & 1))
#define SET_LEAF(x) ((void*)((uintptr_t)x | 1))
#define LEAF_RAW(x) ((art_leaf*)((void*)((uintptr_t)x & ~1)))

#if defined(__GNUC__) && !defined(__clang__)
# if __STDC_VERSION__ >= 199901L && 402 == (__GNUC__ * 100 + __GNUC_MINOR__)
/*
 * GCC 4.2.2's C99 inline keyword support is pretty broken; avoid. Introduced in
 * GCC 4.2.something, fixed in 4.3.0. So checking for specific major.minor of
 * 4.2 is fine.
 */
#  define BROKEN_GCC_C99_INLINE
# endif
#endif

typedef int(*art_callback)(void *data, const unsigned char *key, uint32_t key_len, void *value);

/**
 * This struct is included as part
 * of all the various node sizes
 */
typedef struct {
    uint32_t partial_len;
    uint8_t type;
    uint8_t num_children;
    unsigned char partial[MAX_PREFIX_LEN];
} art_node;

/**
 * Small node with only 4 children
 */
typedef struct {
    art_node n;
    unsigned char keys[4];
    art_node *children[4];
} art_node4;

/**
 * Node with 16 children
 */
typedef struct {
    art_node n;
    unsigned char keys[16];
    art_node *children[16];
} art_node16;

/**
 * Node with 48 children, but
 * a full 256 byte field.
 */
typedef struct {
    art_node n;
    unsigned char keys[256];
    art_node *children[48];
} art_node48;

/**
 * Full node with 256 children
 */
typedef struct {
    art_node n;
    art_node *children[256];
} art_node256;

/**
 * Represents a leaf. These are
 * of arbitrary size, as they include the key.
 */
typedef struct {
    void *value;
    uint32_t key_len;
    unsigned char key[];
} art_leaf;

/**
 * Main struct, points to root.
 */
typedef struct {
    art_node *root;
    uint64_t size;
} art_tree;

class art_trie {
  private:
    art_tree t;
    art_node* alloc_node(uint8_t type) {
      art_node* n;
      switch (type) {
          case NODE4:
              n = (art_node*)calloc(1, sizeof(art_node4));
              break;
          case NODE16:
              n = (art_node*)calloc(1, sizeof(art_node16));
              break;
          case NODE48:
              n = (art_node*)calloc(1, sizeof(art_node48));
              break;
          case NODE256:
              n = (art_node*)calloc(1, sizeof(art_node256));
              break;
          default:
              abort();
      }
      n->type = type;
      return n;
    }
    // Recursively destroys the tree
    void destroy_node(art_node *n) {
      // Break if null
      if (!n) return;
  
      // Special case leafs
      if (IS_LEAF(n)) {
          free(LEAF_RAW(n));
          return;
      }
  
      // Handle each node type
      int i, idx;
      union {
          art_node4 *p1;
          art_node16 *p2;
          art_node48 *p3;
          art_node256 *p4;
      } p;
      switch (n->type) {
          case NODE4:
              p.p1 = (art_node4*)n;
              for (i=0;i<n->num_children;i++) {
                  destroy_node(p.p1->children[i]);
              }
              break;
  
          case NODE16:
              p.p2 = (art_node16*)n;
              for (i=0;i<n->num_children;i++) {
                  destroy_node(p.p2->children[i]);
              }
              break;
  
          case NODE48:
              p.p3 = (art_node48*)n;
              for (i=0;i<256;i++) {
                  idx = ((art_node48*)n)->keys[i]; 
                  if (!idx) continue; 
                  destroy_node(p.p3->children[idx-1]);
              }
              break;
  
          case NODE256:
              p.p4 = (art_node256*)n;
              for (i=0;i<256;i++) {
                  if (p.p4->children[i])
                      destroy_node(p.p4->children[i]);
              }
              break;
  
          default:
              abort();
      }
  
      // Free ourself on the way up
      free(n);
    }
    art_node** find_child(art_node *n, unsigned char c) {
      int i, mask, bitfield;
      union {
          art_node4 *p1;
          art_node16 *p2;
          art_node48 *p3;
          art_node256 *p4;
      } p;
      switch (n->type) {
          case NODE4:
              p.p1 = (art_node4*)n;
              for (i=0 ; i < n->num_children; i++) {
  		/* this cast works around a bug in gcc 5.1 when unrolling loops
  		 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59124
  		 */
                  if (((unsigned char*)p.p1->keys)[i] == c)
                      return &p.p1->children[i];
              }
              break;
  
          {
          case NODE16:
              p.p2 = (art_node16*)n;
  
              // support non-86 architectures
              #ifdef __i386__
                  // Compare the key to all 16 stored keys
                  __m128i cmp;
                  cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                          _mm_loadu_si128((__m128i*)p.p2->keys));
                  
                  // Use a mask to ignore children that don't exist
                  mask = (1 << n->num_children) - 1;
                  bitfield = _mm_movemask_epi8(cmp) & mask;
              #else
              #ifdef __amd64__
                  // Compare the key to all 16 stored keys
                  __m128i cmp;
                  cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                          _mm_loadu_si128((__m128i*)p.p2->keys));
  
                  // Use a mask to ignore children that don't exist
                  mask = (1 << n->num_children) - 1;
                  bitfield = _mm_movemask_epi8(cmp) & mask;
              #else
                  // Compare the key to all 16 stored keys
                  bitfield = 0;
                  for (i = 0; i < 16; ++i) {
                      if (p.p2->keys[i] == c)
                          bitfield |= (1 << i);
                  }
  
                  // Use a mask to ignore children that don't exist
                  mask = (1 << n->num_children) - 1;
                  bitfield &= mask;
              #endif
              #endif
  
              /*
               * If we have a match (any bit set) then we can
               * return the pointer match using ctz to get
               * the index.
               */
              if (bitfield)
                  return &p.p2->children[__builtin_ctz(bitfield)];
              break;
          }
  
          case NODE48:
              p.p3 = (art_node48*)n;
              i = p.p3->keys[c];
              if (i)
                  return &p.p3->children[i-1];
              break;
  
          case NODE256:
              p.p4 = (art_node256*)n;
              if (p.p4->children[c])
                  return &p.p4->children[c];
              break;
  
          default:
              abort();
      }
      return NULL;
    }
    inline int min(int a, int b) {
      return (a < b) ? a : b;
    }
    int check_prefix(const art_node *n, const unsigned char *key, int key_len, int depth) {
      int max_cmp = min(min(n->partial_len, MAX_PREFIX_LEN), key_len - depth);
      int idx;
      for (idx=0; idx < max_cmp; idx++) {
          if (n->partial[idx] != key[depth+idx])
              return idx;
      }
      return idx;
    }
    int leaf_matches(const art_leaf *n, const unsigned char *key, int key_len, int depth) {
      (void)depth;
      // Fail if the key lengths are different
      if (n->key_len != (uint32_t)key_len) return 1;
  
      // Compare the keys starting at the depth
      return memcmp(n->key, key, key_len);
    }
    art_leaf* minimum(const art_node *n) {
        // Handle base cases
        if (!n) return NULL;
        if (IS_LEAF(n)) return LEAF_RAW(n);

        int idx;
        switch (n->type) {
            case NODE4:
                return minimum(((const art_node4*)n)->children[0]);
            case NODE16:
                return minimum(((const art_node16*)n)->children[0]);
            case NODE48:
                idx=0;
                while (!((const art_node48*)n)->keys[idx]) idx++;
                idx = ((const art_node48*)n)->keys[idx] - 1;
                return minimum(((const art_node48*)n)->children[idx]);
            case NODE256:
                idx=0;
                while (!((const art_node256*)n)->children[idx]) idx++;
                return minimum(((const art_node256*)n)->children[idx]);
            default:
                abort();
        }
    }
    // Find the maximum leaf under a node
    art_leaf* maximum(const art_node *n) {
        // Handle base cases
        if (!n) return NULL;
        if (IS_LEAF(n)) return LEAF_RAW(n);

        int idx;
        switch (n->type) {
            case NODE4:
                return maximum(((const art_node4*)n)->children[n->num_children-1]);
            case NODE16:
                return maximum(((const art_node16*)n)->children[n->num_children-1]);
            case NODE48:
                idx=255;
                while (!((const art_node48*)n)->keys[idx]) idx--;
                idx = ((const art_node48*)n)->keys[idx] - 1;
                return maximum(((const art_node48*)n)->children[idx]);
            case NODE256:
                idx=255;
                while (!((const art_node256*)n)->children[idx]) idx--;
                return maximum(((const art_node256*)n)->children[idx]);
            default:
                abort();
        }
    }
    art_leaf* make_leaf(const unsigned char *key, int key_len, void *value) {
        art_leaf *l = (art_leaf*)calloc(1, sizeof(art_leaf)+key_len);
        l->value = value;
        l->key_len = key_len;
        memcpy(l->key, key, key_len);
        return l;
    }

    int longest_common_prefix(art_leaf *l1, art_leaf *l2, int depth) {
        int max_cmp = min(l1->key_len, l2->key_len) - depth;
        int idx;
        for (idx=0; idx < max_cmp; idx++) {
            if (l1->key[depth+idx] != l2->key[depth+idx])
                return idx;
        }
        return idx;
    }

    void copy_header(art_node *dest, art_node *src) {
        dest->num_children = src->num_children;
        dest->partial_len = src->partial_len;
        memcpy(dest->partial, src->partial, min(MAX_PREFIX_LEN, src->partial_len));
    }

    void add_child256(art_node256 *n, art_node **ref, unsigned char c, void *child) {
        (void)ref;
        n->n.num_children++;
        n->children[c] = (art_node*)child;
    }

    void add_child48(art_node48 *n, art_node **ref, unsigned char c, void *child) {
        if (n->n.num_children < 48) {
            int pos = 0;
            while (n->children[pos]) pos++;
            n->children[pos] = (art_node*)child;
            n->keys[c] = pos + 1;
            n->n.num_children++;
        } else {
            art_node256 *new_node = (art_node256*)alloc_node(NODE256);
            for (int i=0;i<256;i++) {
                if (n->keys[i]) {
                    new_node->children[i] = n->children[n->keys[i] - 1];
                }
            }
            copy_header((art_node*)new_node, (art_node*)n);
            *ref = (art_node*)new_node;
            free(n);
            add_child256(new_node, ref, c, child);
        }
    }

    void add_child16(art_node16 *n, art_node **ref, unsigned char c, void *child) {
        if (n->n.num_children < 16) {
            unsigned mask = (1 << n->n.num_children) - 1;
            
            // support non-x86 architectures
            #ifdef __i386__
                __m128i cmp;

                // Compare the key to all 16 stored keys
                cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)n->keys));

                // Use a mask to ignore children that don't exist
                unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
            #else
            #ifdef __amd64__
                __m128i cmp;

                // Compare the key to all 16 stored keys
                cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
                        _mm_loadu_si128((__m128i*)n->keys));

                // Use a mask to ignore children that don't exist
                unsigned bitfield = _mm_movemask_epi8(cmp) & mask;
            #else
                // Compare the key to all 16 stored keys
                unsigned bitfield = 0;
                for (short i = 0; i < 16; ++i) {
                    if (c < n->keys[i])
                        bitfield |= (1 << i);
                }

                // Use a mask to ignore children that don't exist
                bitfield &= mask;    
            #endif
            #endif

            // Check if less than any
            unsigned idx;
            if (bitfield) {
                idx = __builtin_ctz(bitfield);
                memmove(n->keys+idx+1,n->keys+idx,n->n.num_children-idx);
                memmove(n->children+idx+1,n->children+idx,
                        (n->n.num_children-idx)*sizeof(void*));
            } else
                idx = n->n.num_children;

            // Set the child
            n->keys[idx] = c;
            n->children[idx] = (art_node*)child;
            n->n.num_children++;

        } else {
            art_node48 *new_node = (art_node48*)alloc_node(NODE48);

            // Copy the child pointers and populate the key map
            memcpy(new_node->children, n->children,
                    sizeof(void*)*n->n.num_children);
            for (int i=0;i<n->n.num_children;i++) {
                new_node->keys[n->keys[i]] = i + 1;
            }
            copy_header((art_node*)new_node, (art_node*)n);
            *ref = (art_node*)new_node;
            free(n);
            add_child48(new_node, ref, c, child);
        }
    }

    void add_child4(art_node4 *n, art_node **ref, unsigned char c, void *child) {
        if (n->n.num_children < 4) {
            int idx;
            for (idx=0; idx < n->n.num_children; idx++) {
                if (c < n->keys[idx]) break;
            }

            // Shift to make room
            memmove(n->keys+idx+1, n->keys+idx, n->n.num_children - idx);
            memmove(n->children+idx+1, n->children+idx,
                    (n->n.num_children - idx)*sizeof(void*));

            // Insert element
            n->keys[idx] = c;
            n->children[idx] = (art_node*)child;
            n->n.num_children++;

        } else {
            art_node16 *new_node = (art_node16*)alloc_node(NODE16);

            // Copy the child pointers and the key map
            memcpy(new_node->children, n->children,
                    sizeof(void*)*n->n.num_children);
            memcpy(new_node->keys, n->keys,
                    sizeof(unsigned char)*n->n.num_children);
            copy_header((art_node*)new_node, (art_node*)n);
            *ref = (art_node*)new_node;
            free(n);
            add_child16(new_node, ref, c, child);
        }
    }

    void add_child(art_node *n, art_node **ref, unsigned char c, void *child) {
        switch (n->type) {
            case NODE4:
                return add_child4((art_node4*)n, ref, c, child);
            case NODE16:
                return add_child16((art_node16*)n, ref, c, child);
            case NODE48:
                return add_child48((art_node48*)n, ref, c, child);
            case NODE256:
                return add_child256((art_node256*)n, ref, c, child);
            default:
                abort();
        }
    }

    /**
    * Calculates the index at which the prefixes mismatch
    */
    int prefix_mismatch(const art_node *n, const unsigned char *key, int key_len, int depth) {
        int max_cmp = min(min(MAX_PREFIX_LEN, n->partial_len), key_len - depth);
        int idx;
        for (idx=0; idx < max_cmp; idx++) {
            if (n->partial[idx] != key[depth+idx])
                return idx;
        }

        // If the prefix is short we can avoid finding a leaf
        if (n->partial_len > MAX_PREFIX_LEN) {
            // Prefix is longer than what we've checked, find a leaf
            art_leaf *l = minimum(n);
            max_cmp = min(l->key_len, key_len)- depth;
            for (; idx < max_cmp; idx++) {
                if (l->key[idx+depth] != key[depth+idx])
                    return idx;
            }
        }
        return idx;
    }

    void* recursive_insert(art_node *n, art_node **ref, const unsigned char *key, int key_len, void *value, int depth, int *old, int replace) {
        // If we are at a NULL node, inject a leaf
        if (!n) {
            *ref = (art_node*)SET_LEAF(make_leaf(key, key_len, value));
            return NULL;
        }

        // If we are at a leaf, we need to replace it with a node
        if (IS_LEAF(n)) {
            art_leaf *l = LEAF_RAW(n);

            // Check if we are updating an existing value
            if (!leaf_matches(l, key, key_len, depth)) {
                *old = 1;
                void *old_val = l->value;
                if(replace) l->value = value;
                return old_val;
            }

            // New value, we must split the leaf into a node4
            art_node4 *new_node = (art_node4*)alloc_node(NODE4);

            // Create a new leaf
            art_leaf *l2 = make_leaf(key, key_len, value);

            // Determine longest prefix
            int longest_prefix = longest_common_prefix(l, l2, depth);
            new_node->n.partial_len = longest_prefix;
            memcpy(new_node->n.partial, key+depth, min(MAX_PREFIX_LEN, longest_prefix));
            // Add the leafs to the new node4
            *ref = (art_node*)new_node;
            add_child4(new_node, ref, l->key[depth+longest_prefix], SET_LEAF(l));
            add_child4(new_node, ref, l2->key[depth+longest_prefix], SET_LEAF(l2));
            return NULL;
        }

        // Check if given node has a prefix
        if (n->partial_len) {
            // Determine if the prefixes differ, since we need to split
            int prefix_diff = prefix_mismatch(n, key, key_len, depth);
            if ((uint32_t)prefix_diff >= n->partial_len) {
                depth += n->partial_len;
                goto RECURSE_SEARCH;
            }

            // Create a new node
            art_node4 *new_node = (art_node4*)alloc_node(NODE4);
            *ref = (art_node*)new_node;
            new_node->n.partial_len = prefix_diff;
            memcpy(new_node->n.partial, n->partial, min(MAX_PREFIX_LEN, prefix_diff));

            // Adjust the prefix of the old node
            if (n->partial_len <= MAX_PREFIX_LEN) {
                add_child4(new_node, ref, n->partial[prefix_diff], n);
                n->partial_len -= (prefix_diff+1);
                memmove(n->partial, n->partial+prefix_diff+1,
                        min(MAX_PREFIX_LEN, n->partial_len));
            } else {
                n->partial_len -= (prefix_diff+1);
                art_leaf *l = minimum(n);
                add_child4(new_node, ref, l->key[depth+prefix_diff], n);
                memcpy(n->partial, l->key+depth+prefix_diff+1,
                        min(MAX_PREFIX_LEN, n->partial_len));
            }

            // Insert the new leaf
            art_leaf *l = make_leaf(key, key_len, value);
            add_child4(new_node, ref, key[depth+prefix_diff], SET_LEAF(l));
            return NULL;
        }

    RECURSE_SEARCH:;

        // Find a child to recurse to
        art_node **child = find_child(n, key[depth]);
        if (child) {
            return recursive_insert(*child, child, key, key_len, value, depth+1, old, replace);
        }

        // No child, node goes within us
        art_leaf *l = make_leaf(key, key_len, value);
        add_child(n, ref, key[depth], SET_LEAF(l));
        return NULL;
    }
    void remove_child256(art_node256 *n, art_node **ref, unsigned char c) {
        n->children[c] = NULL;
        n->n.num_children--;

        // Resize to a node48 on underflow, not immediately to prevent
        // trashing if we sit on the 48/49 boundary
        if (n->n.num_children == 37) {
            art_node48 *new_node = (art_node48*)alloc_node(NODE48);
            *ref = (art_node*)new_node;
            copy_header((art_node*)new_node, (art_node*)n);

            int pos = 0;
            for (int i=0;i<256;i++) {
                if (n->children[i]) {
                    new_node->children[pos] = n->children[i];
                    new_node->keys[i] = pos + 1;
                    pos++;
                }
            }
            free(n);
        }
    }

    void remove_child48(art_node48 *n, art_node **ref, unsigned char c) {
        int pos = n->keys[c];
        n->keys[c] = 0;
        n->children[pos-1] = NULL;
        n->n.num_children--;

        if (n->n.num_children == 12) {
            art_node16 *new_node = (art_node16*)alloc_node(NODE16);
            *ref = (art_node*)new_node;
            copy_header((art_node*)new_node, (art_node*)n);

            int child = 0;
            for (int i=0;i<256;i++) {
                pos = n->keys[i];
                if (pos) {
                    new_node->keys[child] = i;
                    new_node->children[child] = n->children[pos - 1];
                    child++;
                }
            }
            free(n);
        }
    }

    void remove_child16(art_node16 *n, art_node **ref, art_node **l) {
        int pos = l - n->children;
        memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
        memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
        n->n.num_children--;

        if (n->n.num_children == 3) {
            art_node4 *new_node = (art_node4*)alloc_node(NODE4);
            *ref = (art_node*)new_node;
            copy_header((art_node*)new_node, (art_node*)n);
            memcpy(new_node->keys, n->keys, 4);
            memcpy(new_node->children, n->children, 4*sizeof(void*));
            free(n);
        }
    }

    void remove_child4(art_node4 *n, art_node **ref, art_node **l) {
        int pos = l - n->children;
        memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
        memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
        n->n.num_children--;

        // Remove nodes with only a single child
        if (n->n.num_children == 1) {
            art_node *child = n->children[0];
            if (!IS_LEAF(child)) {
                // Concatenate the prefixes
                int prefix = n->n.partial_len;
                if (prefix < MAX_PREFIX_LEN) {
                    n->n.partial[prefix] = n->keys[0];
                    prefix++;
                }
                if (prefix < MAX_PREFIX_LEN) {
                    int sub_prefix = min(child->partial_len, MAX_PREFIX_LEN - prefix);
                    memcpy(n->n.partial+prefix, child->partial, sub_prefix);
                    prefix += sub_prefix;
                }

                // Store the prefix in the child
                memcpy(child->partial, n->n.partial, min(prefix, MAX_PREFIX_LEN));
                child->partial_len += n->n.partial_len + 1;
            }
            *ref = child;
            free(n);
        }
    }

    void remove_child(art_node *n, art_node **ref, unsigned char c, art_node **l) {
        switch (n->type) {
            case NODE4:
                return remove_child4((art_node4*)n, ref, l);
            case NODE16:
                return remove_child16((art_node16*)n, ref, l);
            case NODE48:
                return remove_child48((art_node48*)n, ref, c);
            case NODE256:
                return remove_child256((art_node256*)n, ref, c);
            default:
                abort();
        }
    }

    art_leaf* recursive_delete(art_node *n, art_node **ref, const unsigned char *key, int key_len, int depth) {
        // Search terminated
        if (!n) return NULL;

        // Handle hitting a leaf node
        if (IS_LEAF(n)) {
            art_leaf *l = LEAF_RAW(n);
            if (!leaf_matches(l, key, key_len, depth)) {
                *ref = NULL;
                return l;
            }
            return NULL;
        }

        // Bail if the prefix does not match
        if (n->partial_len) {
            int prefix_len = check_prefix(n, key, key_len, depth);
            if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len)) {
                return NULL;
            }
            depth = depth + n->partial_len;
        }

        // Find child node
        art_node **child = find_child(n, key[depth]);
        if (!child) return NULL;

        // If the child is leaf, delete from this node
        if (IS_LEAF(*child)) {
            art_leaf *l = LEAF_RAW(*child);
            if (!leaf_matches(l, key, key_len, depth)) {
                remove_child(n, ref, key[depth], child);
                return l;
            }
            return NULL;

        // Recurse
        } else {
            return recursive_delete(*child, child, key, key_len, depth+1);
        }
    }
    int recursive_iter(art_node *n, art_callback cb, void *data) {
        // Handle base cases
        if (!n) return 0;
        if (IS_LEAF(n)) {
            art_leaf *l = LEAF_RAW(n);
            return cb(data, (const unsigned char*)l->key, l->key_len, l->value);
        }

        int idx, res;
        switch (n->type) {
            case NODE4:
                for (int i=0; i < n->num_children; i++) {
                    res = recursive_iter(((art_node4*)n)->children[i], cb, data);
                    if (res) return res;
                }
                break;

            case NODE16:
                for (int i=0; i < n->num_children; i++) {
                    res = recursive_iter(((art_node16*)n)->children[i], cb, data);
                    if (res) return res;
                }
                break;

            case NODE48:
                for (int i=0; i < 256; i++) {
                    idx = ((art_node48*)n)->keys[i];
                    if (!idx) continue;

                    res = recursive_iter(((art_node48*)n)->children[idx-1], cb, data);
                    if (res) return res;
                }
                break;

            case NODE256:
                for (int i=0; i < 256; i++) {
                    if (!((art_node256*)n)->children[i]) continue;
                    res = recursive_iter(((art_node256*)n)->children[i], cb, data);
                    if (res) return res;
                }
                break;

            default:
                abort();
        }
        return 0;
    }
    int leaf_prefix_matches(const art_leaf *n, const unsigned char *prefix, int prefix_len) {
        // Fail if the key length is too short
        if (n->key_len < (uint32_t)prefix_len) return 1;

        // Compare the keys
        return memcmp(n->key, prefix, prefix_len);
    }
    // Returns the size in bytes of the subtrie.
    size_t art_size_in_bytes_at(const art_node *n) {
        if (IS_LEAF(n)) {
            //art_leaf *l = LEAF_RAW(n);
            return sizeof(art_leaf); // + l->key_len;
            // return malloc_size(l) - l->key_len; // sizeof(art_leaf) + l->key_len;
        }
        size_t size = 0;
        union {
            art_node4 *p1;
            art_node16 *p2;
            art_node48 *p3;
            art_node256 *p4;
        } p;
        int i, idx;
        switch (n->type) {
            case NODE4: {
                size += sizeof(art_node4);
                // size += malloc_size(n); // sizeof(art_node4);
                p.p1 = (art_node4*)n;
                for (i=0;i<n->num_children;i++) {
                    size += art_size_in_bytes_at(p.p1->children[i]);
                }
            } break;
            case NODE16: {
                size += sizeof(art_node16);
                // size += malloc_size(n); // sizeof(art_node16);
                p.p2 = (art_node16*)n;
                for (i=0;i<n->num_children;i++) {
                    size += art_size_in_bytes_at(p.p2->children[i]);
                }
            } break;
            case NODE48: {
                size += sizeof(art_node48);
                // size += malloc_size(n); // sizeof(art_node48);
                p.p3 = (art_node48*)n;
                for (i=0;i<256;i++) {
                    idx = ((art_node48*)n)->keys[i]; 
                    if (!idx) continue; 
                    size += art_size_in_bytes_at(p.p3->children[idx-1]);
                }
            } break;
            case NODE256: {
                size += sizeof(art_node256);
                // size += malloc_size(n); // sizeof(art_node256);
                p.p4 = (art_node256*)n;
                for (i=0;i<256;i++) {
                    if (p.p4->children[i])
                        size += art_size_in_bytes_at(p.p4->children[i]);
                }
            } break;
            default:
                assert(false);
                break;
        }
        return size;
    }

  public:
    art_trie() {
        art_tree_init();
    }
    ~art_trie() {
        art_tree_destroy();
    }
    int art_tree_init() {
      t.root = NULL;
      t.size = 0;
      return 0;
    }
    int art_tree_destroy() {
      destroy_node(t.root);
      return 0;
    }
    inline uint64_t art_size() {
      return t.size;
    }
    void* art_insert(const unsigned char *key, int key_len, void *value) {
        int old_val = 0;
        void *old = recursive_insert(t.root, &t.root, key, key_len, value, 0, &old_val, 1);
        if (!old_val) t.size++;
        return old;
    }
    void* art_insert_no_replace(const unsigned char *key, int key_len, void *value) {
        int old_val = 0;
        void *old = recursive_insert(t.root, &t.root, key, key_len, value, 0, &old_val, 0);
        if (!old_val) t.size++;
        return old;
    }
    void* art_delete(const unsigned char *key, int key_len) {
        art_leaf *l = recursive_delete(t.root, &t.root, key, key_len, 0);
        if (l) {
            t.size--;
            void *old = l->value;
            free(l);
            return old;
        }
        return NULL;
    }
    void* art_search(const unsigned char *key, int key_len) {
        art_node **child;
        art_node *n = t.root;
        int prefix_len, depth = 0;
        while (n) {
            // Might be a leaf
            if (IS_LEAF(n)) {
                n = (art_node*)LEAF_RAW(n);
                // Check if the expanded path matches
                if (!leaf_matches((art_leaf*)n, key, key_len, depth)) {
                    return ((art_leaf*)n)->value;
                }
                return NULL;
            }

            // Bail if the prefix does not match
            if (n->partial_len) {
                prefix_len = check_prefix(n, key, key_len, depth);
                if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len))
                    return NULL;
                depth = depth + n->partial_len;
            }

            // Recursively search
            child = find_child(n, key[depth]);
            n = (child) ? *child : NULL;
            depth++;
        }
        return NULL;
    }
    art_leaf* art_minimum() {
        return minimum((art_node*)t.root);
    }
    art_leaf* art_maximum() {
        return maximum((art_node*)t.root);
    }
    int art_iter(art_callback cb, void *data) {
        return recursive_iter(t.root, cb, data);
    }
    int art_iter_prefix(const unsigned char *key, int key_len, art_callback cb, void *data) {
        art_node **child;
        art_node *n = t.root;
        int prefix_len, depth = 0;
        while (n) {
            // Might be a leaf
            if (IS_LEAF(n)) {
                n = (art_node*)LEAF_RAW(n);
                // Check if the expanded path matches
                if (!leaf_prefix_matches((art_leaf*)n, key, key_len)) {
                    art_leaf *l = (art_leaf*)n;
                    return cb(data, (const unsigned char*)l->key, l->key_len, l->value);
                }
                return 0;
            }

            // If the depth matches the prefix, we need to handle this node
            if (depth == key_len) {
                art_leaf *l = minimum(n);
                if (!leaf_prefix_matches(l, key, key_len))
                return recursive_iter(n, cb, data);
                return 0;
            }

            // Bail if the prefix does not match
            if (n->partial_len) {
                prefix_len = prefix_mismatch(n, key, key_len, depth);

                // Guard if the mis-match is longer than the MAX_PREFIX_LEN
                if ((uint32_t)prefix_len > n->partial_len) {
                    prefix_len = n->partial_len;
                }

                // If there is no match, search is terminated
                if (!prefix_len) {
                    return 0;

                // If we've matched the prefix, iterate on this node
                } else if (depth + prefix_len == key_len) {
                    return recursive_iter(n, cb, data);
                }

                // if there is a full match, go deeper
                depth = depth + n->partial_len;
            }

            // Recursively search
            child = find_child(n, key[depth]);
            n = (child) ? *child : NULL;
            depth++;
        }
        return 0;
    }
    size_t art_size_in_bytes() {
        size_t size = sizeof(art_tree);
        if (t.root != NULL) {
            size += art_size_in_bytes_at(t.root);
        }
        return size;
    }

};

} // namespace art

#endif // ifdef art
