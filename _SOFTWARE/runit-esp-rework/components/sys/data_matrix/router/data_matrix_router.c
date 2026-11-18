#include "data_matrix_router.h"

#include "data_matrix_internal.h"

ROUTER_WRITE(DATA_MATRIX, OP_ROLE) {
  return data_matrix_set_role(args);
}

ROUTER_WRITE(DATA_MATRIX, OP_CLEAR_ROLE) {
  return data_matrix_clear_role(args->provider);
}

ROUTER_WRITE(DATA_MATRIX, OP_ROUTE) {
  return data_matrix_route(args);
}

ROUTER_WRITE(DATA_MATRIX, OP_UNROUTE) {
  return data_matrix_unroute(args);
}

ROUTER_WRITE(DATA_MATRIX, OP_CLEAR) {
  return data_matrix_clear(args);
}

// LIST: u8 roles, a role's args each, u8 routes, a route each.
ROUTER_READ(DATA_MATRIX, OP_LIST) {
  data_matrix_role_args_t roles[CONFIG_DATA_MATRIX_ROLES];
  data_matrix_route_t routes[CONFIG_DATA_MATRIX_ROUTES];
  uint8_t role_count = 0;
  uint8_t route_count = 0;
  data_matrix_list(roles, &role_count, routes, &route_count);
  ERR_TRY(cmd_append(out, &role_count, sizeof(role_count)));
  ERR_TRY(cmd_append(out, roles, role_count * sizeof(roles[0])));
  ERR_TRY(cmd_append(out, &route_count, sizeof(route_count)));
  return cmd_append(out, routes, route_count * sizeof(routes[0]));
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(DATA_MATRIX, OP_ROLE),  ROUTER_ENTRY_W(DATA_MATRIX, OP_CLEAR_ROLE),
    ROUTER_ENTRY_W(DATA_MATRIX, OP_ROUTE), ROUTER_ENTRY_W(DATA_MATRIX, OP_UNROUTE),
    ROUTER_ENTRY_W(DATA_MATRIX, OP_CLEAR), ROUTER_ENTRY_R(DATA_MATRIX, OP_LIST),
};

ROUTER_MODULE(data_matrix_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_DATA_MATRIX, data_matrix_router)
