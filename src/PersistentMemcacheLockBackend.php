<?php

/**
 * @file
 * Contains \Drupal\memcache\PersistentMemcacheLockBackend.
 */

namespace Drupal\memcache;

class PersistentMemcacheLockBackend extends MemcacheLockBackend {

  /**
   * Constructs a new MemcacheLockBackend.
   *
   * @param \Drupal\Memcache\DrupalMemcacheFactory $memcache_factory
   */
  public function __construct(DrupalMemcacheFactory $memcache_factory) {
    // Do not call the parent constructor to avoid registering a shutdwon
    // function that will release all locks at the end of the request.
    $this->memcache = $memcache_factory->get($this->bin);
    // Set the lockId to a fixed string to make the lock ID the same across
    // multiple requests. The lock ID is used as a page token to relate all the
    // locks set during a request to each other.
    // @see \Drupal\Core\Lock\LockBackendInterface::getLockId()
    $this->lockId = 'persistent';
  }

}
