<?php

/**
 * @file
 * Contains \Drupal\memcache\MemcacheLockBackend.
 */

namespace Drupal\memcache;

use Drupal\Core\Lock\LockBackendAbstract;

/**
 * Defines a Memcache lock backend.
 */
class MemcacheLockBackend extends LockBackendAbstract {

  /**
   * @var array
   */
  protected $locks = array();

  /**
   * @var string
   */
  protected $bin = 'semaphore';

  /**
   * Constructs a new MemcacheLockBackend.
   */
  public function __construct() {
    // __destruct() is causing problems with garbage collections, register a
    // shutdown function instead.
    drupal_register_shutdown_function(array($this, 'releaseAll'));
  }

  /**
   * {@inheritdoc}
   */
  public function acquire($name, $timeout = 30.0) {
    // Ensure that the timeout is at least 1 sec. This is a limitation imposed
    // by memcached.
    $timeout = (int) max($timeout, 1);
    $lock_id = $this->getLockId();

    if (DrupalMemcache::add($name, $lock_id, $timeout, $this->bin)) {
      $this->locks[$name] = $lock_id;
    }
    elseif (($result = DrupalMemcache::get($name, $this->bin)) && isset($this->locks[$name]) && ($this->locks[$name] == $lock_id)) {
      // Only renew the lock if we already set it and it has not expired.
      DrupalMemcache::set($name, $lock_id, $timeout, $this->bin);
    }
    else {
      // Failed to acquire the lock. Unset the key from the $locks array even if
      // not set.
      unset($this->locks[$name]);
    }

    return isset($this->locks[$name]);
  }

  /**
   * {@inheritdoc}
   */
  public function lockMayBeAvailable($name) {
    return !DrupalMemcache::get($name, $this->bin);
  }

  /**
   * {@inheritdoc}
   */
  public function release($name) {
    DrupalMemcache::delete($name, $this->bin);
    // We unset unconditionally since caller assumes lock is released anyway.
    unset($this->locks[$name]);
  }

  /**
   * {@inheritdoc}
   */
  public function releaseAll($lock_id = NULL) {
    foreach ($this->locks as $name => $id) {
      $value = DrupalMemcache::get($name, $this->bin);
      if ($value == $id) {
        DrupalMemcache::delete($name, $this->bin);
      }
    }
  }

}
