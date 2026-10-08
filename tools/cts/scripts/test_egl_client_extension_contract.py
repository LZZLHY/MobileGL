"""只校验扩展广告标准判据；这些内存记录不声称真实surface/driver资格。"""
import importlib.util,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('egl_client_contract',Path(__file__).parents[1]/'probe/egl_client_extension_contract.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class Contract(unittest.TestCase):
 def test_proc_supported_without_direct_export_is_not_rejected(self):
  """规范不强制direct export：已有proc的工具判据不能错误要求ELF导出。"""
  self.assertEqual(module.client_contract_issues({'EGL_EXT_platform_base'},[{'proc':1,'export':None} for _ in range(3)]),[])
 def test_missing_proc_even_with_export_is_rejected(self):
  """advertised命令缺proc时不能用单独export补合同；该测试只验证工具规则。"""
  self.assertTrue(module.client_contract_issues({'EGL_EXT_platform_base'},[{'proc':None,'export':1}]))
 def test_withdrawn_capability_does_not_qualify_absent_commands(self):
  """无广告可保留未解析命令，但规则接受不代表其语义或surface已实现。"""
  self.assertEqual(module.client_contract_issues(set(),[{'proc':None,'export':None}]),[])
if __name__=='__main__':unittest.main()
