<?php

namespace Drupal\memcache_test;

use Drupal\Core\DependencyInjection\ContainerBuilder;
use Drupal\Core\DependencyInjection\ServiceModifierInterface;
use Symfony\Component\DependencyInjection\Reference;

/**
 * Test service provider.
 */
class MemcacheTestServiceProvider implements ServiceModifierInterface {

  /**
   * {@inheritdoc}
   */
  public function alter(ContainerBuilder $container) {
    // Alter the lock definition to use the memcache lock class.
    $definition = $container->getDefinition('lock');

    $definition->setClass('Drupal\memcache\MemcacheLockBackend');
    $definition->setArguments([new Reference('memcache.factory')]);
    // @todo Could make this lazy again but need to create a proxy.
    $definition->setLazy(FALSE);
  }

}
